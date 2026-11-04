// Web version glue, loaded before the front-end's own script (scripts/build-web.mjs puts it into index.html, with the
// landing page from web/landing.html). The emulator runs in worker.js, started from the landing page; the
// front-end's fetch() calls to the emulator API (/screen, /key, /dial, ...) are answered by the worker instead of a
// server, so ui/index.html works unchanged.
(function () {
  window.BVE_WEB = true;
  const DB_NAME = 'bve9100', STORE = 'files';
  let worker = null, running = false, seq = 0;
  const pending = new Map();
  const $ = (id) => document.getElementById(id);

  // ---- top bar, as the MAXINE! Emulator's: name, Stop, Reset…, Palette, status, speed; and a link to it ------
  const top = $('top'), pal = $('palbtn');
  top.querySelectorAll(':scope > span').forEach((s) => s.remove());
  const button = (text, title, onclick) => {
    const b = document.createElement('button');
    b.textContent = text; b.title = title; b.onclick = onclick;
    top.insertBefore(b, pal);
    return b;
  };
  button('Stop', 'Save and go back to the start page', stop);
  button('Reset…', 'Start again from the default machine', () => {
    if (worker && confirm('Go back to the default setup? This deletes the setup, EDL and palette saved in this browser.')) worker.postMessage({ type: 'reset' });
  });
  const statusEl = document.createElement('span'), speedEl = document.createElement('span');
  statusEl.id = 'webstatus'; speedEl.id = 'webspeed';
  const maxine = document.createElement('a');
  maxine.id = 'maxinelink'; maxine.href = 'https://maxine.retrograde.tools'; maxine.target = '_blank'; maxine.rel = 'noopener';
  maxine.textContent = 'MAXINE! Emulator ↗';
  top.append(statusEl, speedEl, maxine);
  const note = document.querySelector('#palpanel .note');
  if (note) note.innerHTML = 'Changes preview on the screen; <b>Inject</b> writes them into the setup database in SRAM (as INIT &gt; ' +
    'LAYOUT &gt; PALETTE would) and the editor uses them at once. This browser keeps them. Colour 0 draws the frame lines ' +
    '(the menu cannot change it), 8 is the default screen background.';

  // ---- landing page ---------------------------------------------------------------------------------------------
  const landing = $('landing'), startBtn = $('startbtn'), landStatus = $('landstatus'), landReset = $('landreset');
  function showSaved(saved) {
    startBtn.textContent = saved ? 'Continue' : 'Start emulator';
    landReset.hidden = !saved;
  }
  function openDb() {
    return new Promise((resolve, reject) => {
      const r = indexedDB.open(DB_NAME, 1);
      r.onupgradeneeded = () => r.result.createObjectStore(STORE);
      r.onsuccess = () => resolve(r.result);
      r.onerror = () => reject(r.error);
    });
  }
  async function dbDo(mode, fn) {
    const db = await openDb();
    return new Promise((resolve, reject) => {
      const tx = db.transaction(STORE, mode), r = fn(tx.objectStore(STORE));
      tx.oncomplete = () => { db.close(); resolve(r.result); };
      tx.onerror = () => { db.close(); reject(tx.error); };
    });
  }
  const hasSaved = () => dbDo('readonly', (s) => s.count()).then((n) => n > 0).catch(() => false);
  hasSaved().then(showSaved);

  startBtn.onclick = start;
  landReset.onclick = async () => {
    await dbDo('readwrite', (s) => s.clear()).catch(() => {});
    showSaved(false);
    landStatus.textContent = 'This browser starts from the default setup next time.';
  };

  function setStatus(text) { statusEl.textContent = text; if (!running) landStatus.textContent = text; }

  function start() {
    if (worker) return;
    startBtn.disabled = true; startBtn.textContent = 'Starting…'; landReset.hidden = true;
    worker = new Worker('worker.js');
    worker.onmessage = (e) => {
      const m = e.data;
      if (m.type === 'res') {
        const resolve = pending.get(m.id); pending.delete(m.id);
        resolve(new Response(m.body, { status: m.status, headers: { 'content-type': m.ctype } }));
      } else if (m.type === 'status') setStatus(m.text);
      else if (m.type === 'speed') speedEl.textContent = m.pct == null ? '' : `${m.pct}%`;
      else if (m.type === 'running') {
        running = true; landing.hidden = true; startBtn.disabled = false;
        document.activeElement?.blur();      // the hidden Start button would otherwise keep the keyboard (Firefox)
      }
      else if (m.type === 'failed') { setStatus(m.text); worker.terminate(); worker = null; startBtn.disabled = false; hasSaved().then(showSaved); }
      else if (m.type === 'stopped') {
        worker.terminate(); worker = null; running = false;
        for (const resolve of pending.values()) resolve(new Response('stopped', { status: 503 }));
        pending.clear();
        statusEl.textContent = speedEl.textContent = landStatus.textContent = '';
        startBtn.disabled = false; showSaved(true); landing.hidden = false;
      } else if (m.type === 'reload') location.reload();
    };
  }

  function stop() {
    if (worker && running) worker.postMessage({ type: 'stop' });
  }

  // ---- the front-end's API calls go to the worker -------------------------------------------------------------
  const API = /^\/(screen|key|dial|devices|status|palette)(\/|$)/;
  const realFetch = window.fetch.bind(window);
  window.fetch = (input, init = {}) => {
    const url = new URL(typeof input === 'string' ? input : input.url, location.href);
    if (url.origin !== location.origin || !API.test(url.pathname)) return realFetch(input, init);
    if (!worker) return Promise.resolve(new Response('not running', { status: 503 }));
    return new Promise((resolve) => {
      const id = ++seq;
      pending.set(id, resolve);
      worker.postMessage({ type: 'req', id, method: (init.method || 'GET').toUpperCase(), path: url.pathname, body: init.body == null ? '' : String(init.body) });
    });
  };

  // Save when the page goes away or into the background (the worker also saves every 10 s)
  const save = () => worker && worker.postMessage({ type: 'save' });
  addEventListener('pagehide', save);
  document.addEventListener('visibilitychange', () => { if (document.hidden) save(); });
})();
