#!/usr/bin/env node
// Builds the web version into dist-web/, a static site for Cloudflare Pages (npm run deploy:web) or any host:
//
//   index.html        ui/index.html with the landing page (web/landing.html, with web/CHANGELOG.md in it), its
//                     styles (web/web.css) and web/web.js loaded before the front-end's script
//   web.js worker.js  the page glue and the worker that runs the emulator (web/), icon.svg, favicon.ico, assets/
//   emulator/         bve9100.js + .wasm (Emscripten build in build-web/), the default machine from web/defaults/
//                     gzipped, and manifest.json naming them with a hash of the defaults (a browser drops its saved
//                     machine when that changes)
//
// Needs Emscripten in ~/emsdk or $EMSDK (git clone https://github.com/emscripten-core/emsdk ~/emsdk; ./emsdk
// install latest; ./emsdk activate latest). BVE_SKIP_EMULATOR=1 rebuilds only the site from the last build.
// The default machine is web/defaults/ (scripts/snapshot-defaults.mjs samples it from the running emulator).
import { execFileSync } from 'node:child_process';
import { copyFileSync, cpSync, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { homedir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { gzipSync } from 'node:zlib';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const BUILD = join(ROOT, 'build-web');
const SITE = join(ROOT, 'dist-web');
const EMU = join(SITE, 'emulator');
const DEFAULTS = join(ROOT, 'web/defaults');
const MACHINE = [
  { name: 'rms.abs' }, { name: 'a.img' }, { name: 'b.img' }, { name: 'sram.bin' }, { name: 'eeprom.bin', optional: true },
];

if (!process.env.BVE_SKIP_EMULATOR) {
  const emsdk = process.env.EMSDK || join(homedir(), 'emsdk');
  if (!existsSync(join(emsdk, 'emsdk_env.sh'))) throw new Error(`No Emscripten SDK in ${emsdk} (see the top of this script)`);
  execFileSync('bash', ['-c',
    `command -v emcc >/dev/null || . "${emsdk}/emsdk_env.sh" >/dev/null 2>&1
     emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release && cmake --build build-web -j`], { cwd: ROOT, stdio: 'inherit' });
}

rmSync(SITE, { recursive: true, force: true });
mkdirSync(EMU, { recursive: true });
for (const f of ['bve9100.js', 'bve9100.wasm']) copyFileSync(join(BUILD, f), join(EMU, f));
// The page's own files go by a version (?v=hash of the file) so no browser runs a stale copy, whatever the server's
// caching (a server that sends no Cache-Control lets Firefox keep files across restarts)
const version = (path) => createHash('sha256').update(readFileSync(path)).digest('hex').slice(0, 10);
for (const f of ['worker.js', 'web.css', 'icon.svg', 'favicon.ico']) copyFileSync(join(ROOT, 'web', f), join(SITE, f));
const workerV = version(join(SITE, 'worker.js'));
writeFileSync(join(SITE, 'web.js'), readFileSync(join(ROOT, 'web/web.js'), 'utf8').replace("new Worker('worker.js')", `new Worker('worker.js?v=${workerV}')`));
cpSync(join(ROOT, 'web/assets'), join(SITE, 'assets'), { recursive: true });

// The changelog as written: # headings, - or * bullets, and every other line as text
const esc = (t) => t.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
let changelog = '', inList = false;
for (const raw of readFileSync(join(ROOT, 'web/CHANGELOG.md'), 'utf8').split('\n')) {
  const line = raw.trim();
  if (!line) continue;
  const heading = /^#+\s+(.*)$/.exec(line), item = /^[-*]\s+(.*)$/.exec(line);
  if (inList && !item) { changelog += '</ul>'; inList = false; }
  if (heading) changelog += `<h3>${esc(heading[1])}</h3>`;
  else if (!item) changelog += `<p>${esc(line)}</p>`;
  else { if (!inList) { changelog += '<ul>'; inList = true; } changelog += `<li>${esc(item[1])}</li>`; }
}
if (inList) changelog += '</ul>';

// Unfurlers (Slack and the like) show the site's icon beside the title, but not an SVG one: they take the PNGs
// (web/assets/icon-32.png, apple-touch-icon.png) or favicon.ico, all rendered from web/icon.svg. The thumbnail
// beside the text is og:image (icon-512.png; an absolute URL, as unfurlers want), small because the card is "summary"
const ICON_URL = `https://bve.retrograde.tools/assets/icon-512.png?v=${version(join(ROOT, 'web/assets/icon-512.png'))}`;
const HEAD = `<title>BVE-9100 Emulator — Retrograde Labs</title>
<meta name="description" content="The Sony BVE-9100, emulated in full and running in your browser.">
<meta property="og:title" content="BVE-9100 Emulator">
<meta property="og:description" content="The Sony BVE-9100, emulated in full and running in your browser.">
<meta property="og:site_name" content="Retrograde Labs">
<meta property="og:image" content="${ICON_URL}">
<meta property="og:image:width" content="512">
<meta property="og:image:height" content="512">
<meta name="twitter:card" content="summary">
<link rel="icon" type="image/svg+xml" href="icon.svg">
<link rel="icon" type="image/png" sizes="32x32" href="assets/icon-32.png">
<link rel="apple-touch-icon" sizes="180x180" href="assets/apple-touch-icon.png">
<link rel="stylesheet" href="web.css?v=${version(join(SITE, 'web.css'))}">`;
const landing = readFileSync(join(ROOT, 'web/landing.html'), 'utf8').replace('<!--CHANGELOG-->', changelog);
const splice = (page, find, make) => {
  const at = page.indexOf(find);
  if (at < 0) throw new Error(`ui/index.html: no ${find.trim()}`);
  return make(page.slice(0, at), page.slice(at));
};
let page = readFileSync(join(ROOT, 'ui/index.html'), 'utf8');
page = page.replace(/<title>[^<]*<\/title>/, '');
page = splice(page, '</head>', (a, b) => a + HEAD + '\n' + b);
page = splice(page, '<body>', (a, b) => a + '<body>\n' + landing + b.slice('<body>'.length));
page = splice(page, '\n<script>', (a, b) => a + `\n<script src="web.js?v=${version(join(SITE, 'web.js'))}"></script>` + b);
writeFileSync(join(SITE, 'index.html'), page);

const hash = createHash('sha256'), files = [];
for (const { name, optional } of MACHINE) {
  const path = join(DEFAULTS, name);
  if (!existsSync(path)) {
    if (optional) continue;
    throw new Error(`web/defaults/${name} is missing: run node scripts/snapshot-defaults.mjs`);
  }
  const data = readFileSync(path);
  hash.update(name).update(data);
  writeFileSync(join(EMU, `${name}.gz`), gzipSync(data, { level: 9 }));
  files.push({ name, url: `${name}.gz` });
}
writeFileSync(join(EMU, 'manifest.json'), JSON.stringify({ image: hash.digest('hex').slice(0, 16), files }, null, 2));
// Make browsers check for a newer page / emulator / machine each time (an unchanged file is a quick 304)
writeFileSync(join(SITE, '_headers'), '/*\n  Cache-Control: no-cache\n');
console.log('dist-web is ready (try it: npm run preview:web)');
