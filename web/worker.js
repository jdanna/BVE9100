// Web version: runs the emulator (emulator/bve9100.js + .wasm, built by scripts/build-web.mjs) in a worker.
//
// The page (web.js) sends the front-end's API requests here as messages instead of HTTP: {type:'req', id, method,
// path, body} -> {type:'res', id, status, ctype, body}. The machine boots in fast-forward like the native front-end
// (--http: flat out to TURBO_UNTIL cycles), then runs paced to real time. What it writes (SRAM, EEPROM, floppies) is
// kept in IndexedDB and used instead of the defaults next time, as long as the defaults haven't changed.
importScripts('emulator/bve9100.js');

const CPU_HZ = 20e6;
const TURBO_UNTIL = 2.7e9;
const SAVE_MS = 10000;
const WRITABLE = ['a.img', 'b.img', 'sram.bin', 'eeprom.bin'];
const DB_NAME = 'bve9100', STORE = 'files';

let M = null, db = null, ready = false;
let request, responseStatus, responseType;
const saved = {};                       // what IndexedDB holds now, to skip unchanged files

const post = (m) => self.postMessage(m);
const status = (text) => post({ type: 'status', text });
const reply = (id, st, ctype, body) => post({ type: 'res', id, status: st, ctype, body });

self.onmessage = (e) => {
  const m = e.data;
  if (m.type === 'req') {
    if (!ready) return reply(m.id, 503, 'text/plain', 'the machine is starting');
    const body = request(m.method, m.path, m.body || '');
    reply(m.id, responseStatus(), responseType(), body);
  } else if (m.type === 'save') save();
  else if (m.type === 'stop') stop();
  else if (m.type === 'reset') reset();
};

// ---- IndexedDB -------------------------------------------------------------------------------------------
function openDb() {
  return new Promise((resolve, reject) => {
    const r = indexedDB.open(DB_NAME, 1);
    r.onupgradeneeded = () => r.result.createObjectStore(STORE);
    r.onsuccess = () => resolve(r.result);
    r.onerror = () => reject(r.error);
  });
}
function dbOp(mode, fn) {
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE, mode), r = fn(tx.objectStore(STORE));
    tx.oncomplete = () => resolve(r && r.result);
    tx.onerror = () => reject(tx.error);
  });
}
const dbGet = (k) => dbOp('readonly', (s) => s.get(k));
const dbPut = (k, v) => dbOp('readwrite', (s) => s.put(v, k));
const dbClear = () => dbOp('readwrite', (s) => s.clear());

// ---- downloads (gzipped; some servers unpack on the way, so look at what arrived) --------------------------
async function download(url) {
  const r = await fetch(url, { cache: 'no-cache' });
  if (!r.ok) throw new Error(`${url}: ${r.status} ${r.statusText}`);
  const data = new Uint8Array(await r.arrayBuffer());
  if (data[0] !== 0x1f || data[1] !== 0x8b) return data;
  return new Uint8Array(await new Response(new Blob([data]).stream().pipeThrough(new DecompressionStream('gzip'))).arrayBuffer());
}

// ---- start -------------------------------------------------------------------------------------------------
async function start() {
  status('Downloading the machine…');
  const manifest = await (await fetch('emulator/manifest.json', { cache: 'no-cache' })).json();
  db = await openDb();
  const savedImage = await dbGet('image');
  if (savedImage && savedImage !== manifest.image) {
    await dbClear();
    status('The default machine has changed, so this browser starts from the new one.');
  }
  const files = {};
  for (const f of manifest.files) {
    let data = savedImage === manifest.image && WRITABLE.includes(f.name) ? await dbGet(f.name) : undefined;
    if (!data) {
      data = await download(`emulator/${f.url}`);
      if (WRITABLE.includes(f.name)) await dbPut(f.name, data);   // kept from the start, so the next visit downloads nothing
    }
    if (WRITABLE.includes(f.name)) saved[f.name] = data;
    files[f.name] = data;
  }
  await dbPut('image', manifest.image);

  status('Loading the emulator…');
  M = await createBve({
    locateFile: (p) => `emulator/${p}`,
    print: (t) => console.log(t),
    printErr: (t) => console.log(t),
  });
  M.FS.mkdir('/bve');
  for (const [name, data] of Object.entries(files)) M.FS.writeFile(`/bve/${name}`, data);
  if (!M._bve_init()) { post({ type: 'failed', text: 'The machine could not boot (see the browser console).' }); return; }
  request = M.cwrap('bve_request', 'string', ['string', 'string', 'string']);
  responseStatus = M._bve_response_status;
  responseType = () => M.UTF8ToString(M._bve_response_type());
  ready = true;
  post({ type: 'running' });
  setInterval(save, SAVE_MS);
  loop();
}

// ---- pacing: fast-forward through the boot, then real time ------------------------------------------------
let base = null, lastReport = 0, speedAt = null;
function loop() {
  if (!ready) return;
  const t0 = performance.now();
  let cyc = M._bve_cycles();
  if (cyc < TURBO_UNTIL) {
    while (performance.now() - t0 < 15 && cyc < TURBO_UNTIL) { M._bve_step(200000); cyc = M._bve_cycles(); }
    if (t0 - lastReport > 500) { lastReport = t0; status(`Starting up… ${Math.floor(cyc / TURBO_UNTIL * 100)}%`); }
    if (cyc >= TURBO_UNTIL) { base = speedAt = { wall: performance.now(), cycles: cyc }; status(''); }
  } else {
    const now = performance.now();
    let target = base.cycles + (now - base.wall) * CPU_HZ / 1000;
    if (target - cyc > CPU_HZ / 4) { base = { wall: now, cycles: cyc }; target = cyc; }   // fell behind (tab asleep): carry on from now
    while (cyc < target && performance.now() - now < 30) { M._bve_step(Math.min(100000, target - cyc)); cyc = M._bve_cycles(); }
    if (now - speedAt.wall >= 1000) {         // how close to a real 20 MHz 68020 it keeps up
      post({ type: 'speed', pct: Math.round((cyc - speedAt.cycles) / ((now - speedAt.wall) * CPU_HZ / 1000) * 100) });
      speedAt = { wall: now, cycles: cyc };
    }
  }
  setTimeout(loop, 0);
}

// ---- persistence -------------------------------------------------------------------------------------------
const same = (a, b) => a && b && a.length === b.length && a.every((v, i) => v === b[i]);
let saving = Promise.resolve();
const save = () => (saving = saving.then(saveNow));   // one at a time, and a save asked for during one still runs
async function saveNow() {
  if (!ready) return;
  try {
    M._bve_save();
    for (const name of WRITABLE) {
      let data; try { data = M.FS.readFile(`/bve/${name}`); } catch { continue; }
      if (same(saved[name], data)) continue;
      await dbPut(name, data); saved[name] = data;
    }
  } catch (e) { console.log('save failed', e); }
}
async function stop() {
  await save();
  ready = false;
  post({ type: 'stopped' });
}
async function reset() {
  ready = false;
  if (!db) db = await openDb();
  await dbClear();
  post({ type: 'reload' });
}

start().catch((e) => { post({ type: 'failed', text: `The emulator stopped: ${e.message}` }); console.error(e); });
