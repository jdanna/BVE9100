#!/usr/bin/env node
// Samples the machine the web version starts from into web/defaults/: SRAM (setup database: device assignment,
// layout, palette, EDL), EEPROM and both floppies, plus the boot loader (work/raw/rms.abs).
//
// Each file comes from a running emulator if one answers on PORT (default 8090: GET /sram, /eeprom, /disk/a,
// /disk/b, so it is the state right now), or else from work/run/, which holds what the emulator saved when it was
// last stopped with Ctrl-C. Usage: npm run snapshot:web   (PORT=9000 for another port)
import { copyFileSync, existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const OUT = join(ROOT, 'web/defaults');
const PORT = process.env.PORT || 8090;
const FILES = [
  { name: 'sram.bin', endpoint: '/sram', file: 'work/run/sram.bin', size: 0x100000 },
  { name: 'eeprom.bin', endpoint: '/eeprom', file: 'work/run/eeprom.bin', size: 0x800, optional: true },
  { name: 'a.img', endpoint: '/disk/a', file: 'work/run/a.img', size: 1474560 },
  { name: 'b.img', endpoint: '/disk/b', file: 'work/run/b.img', size: 1474560 },
];

mkdirSync(OUT, { recursive: true });
for (const f of FILES) {
  let data = null, from = '';
  try {
    const r = await fetch(`http://127.0.0.1:${PORT}${f.endpoint}`, { signal: AbortSignal.timeout(3000) });
    if (r.ok) { data = new Uint8Array(await r.arrayBuffer()); from = `the running emulator (port ${PORT})`; }
  } catch {}
  if ((!data || data.length !== f.size) && existsSync(join(ROOT, f.file))) { data = readFileSync(join(ROOT, f.file)); from = f.file; }
  if (!data) {
    if (f.optional) { console.log(`${f.name}: none (the machine starts with a blank one)`); continue; }
    throw new Error(`${f.name}: no running emulator answers on port ${PORT} and there is no ${f.file}`);
  }
  if (data.length !== f.size) throw new Error(`${f.name}: ${data.length} bytes from ${from}, expected ${f.size}`);
  writeFileSync(join(OUT, f.name), data);
  console.log(`${f.name}: from ${from}`);
}
copyFileSync(join(ROOT, 'work/raw/rms.abs'), join(OUT, 'rms.abs'));
console.log('rms.abs: from work/raw/rms.abs');
console.log(`web/defaults is ready; rebuild the site with npm run build:web`);
