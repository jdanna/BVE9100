#!/usr/bin/env node
// Boots dist-web in headless Chrome and checks what a visitor depends on: the machine boots and keeps real time,
// the decks are assigned, a key reaches the software, the setup it writes lands in the browser, and a second visit
// runs from that copy without downloading the floppies again. Build first with `npm run build:web`. CHROME_PATH
// picks the browser.

import { spawn } from 'node:child_process'
import { existsSync, mkdirSync, mkdtempSync, rmSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { serve } from './web-server.mjs'

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..')
const PORT = 8092
const DEBUG_PORT = 9335
const BROWSERS = ['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser', '/snap/bin/chromium',
  '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome']
const BOOT_MS = 240000 // the fast-forward through the boot takes about 45 s on a desktop
const SAVE_POLLS = 40 // half-second tries, because saves come 10 seconds apart
const MIN_SPEED = 90
const KEY_HOLD_MS = 130

const sleep = (ms) => new Promise((r) => setTimeout(r, ms))

const browser = process.env.CHROME_PATH ?? BROWSERS.find(existsSync)
if (!browser) throw new Error('No Chrome found. Set CHROME_PATH.')

const requests = []
const server = await serve(PORT, (path) => requests.push(path))
// Under the repository rather than /tmp: a snap Chromium can only write inside the home directory
mkdirSync(join(ROOT, 'build-web'), { recursive: true })
const profile = mkdtempSync(join(ROOT, 'build-web', 'chrome-'))
// These flags keep the page visible. Without them a synthetic key can mark it hidden, and that slows its timers.
const chrome = spawn(
  browser,
  [
    '--headless=new', `--remote-debugging-port=${DEBUG_PORT}`, `--user-data-dir=${profile}`, '--no-first-run',
    '--window-size=1600,1000', '--disable-backgrounding-occluded-windows', '--disable-renderer-backgrounding',
    '--disable-background-timer-throttling', 'about:blank'
  ],
  { stdio: 'ignore' }
)

let failed = false
function check(ok, what) {
  console.log(`${ok ? 'PASS' : 'FAIL'}: ${what}`)
  if (!ok) failed = true
}

try {
  let targets = []
  for (let i = 0; i < 100 && !targets.some((t) => t.type === 'page'); i++) {
    await sleep(100)
    targets = await fetch(`http://127.0.0.1:${DEBUG_PORT}/json`).then((r) => r.json(), () => [])
  }
  const socket = new WebSocket(targets.find((t) => t.type === 'page').webSocketDebuggerUrl)
  await new Promise((resolve) => (socket.onopen = resolve))

  let nextId = 0
  const pending = new Map()
  socket.onmessage = ({ data }) => {
    const message = JSON.parse(data)
    pending.get(message.id)?.(message)
    pending.delete(message.id)
  }
  const send = (method, params = {}) =>
    new Promise((resolve) => {
      pending.set(++nextId, resolve)
      socket.send(JSON.stringify({ id: nextId, method, params }))
    })
  const evaluate = async (expression) =>
    (await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true })).result?.result?.value
  const press = async (key, code, vk) => {
    const event = { key, code, windowsVirtualKeyCode: vk, nativeVirtualKeyCode: vk }
    await send('Input.dispatchKeyEvent', { type: 'rawKeyDown', ...event })
    await sleep(KEY_HOLD_MS)
    await send('Input.dispatchKeyEvent', { type: 'keyUp', ...event })
    await sleep(KEY_HOLD_MS)
  }
  const button = (name) => `[...document.querySelectorAll('button')].find((b) => b.textContent.trim() === '${name}')`
  const start = async (startLabel) => {
    await send('Page.navigate', { url: `http://127.0.0.1:${PORT}/` })
    await sleep(1500)
    await evaluate(`${button(startLabel)}.click()`)
    for (let i = 0; i < 200 && !(await evaluate(`document.getElementById('landing').hidden`)); i++) await sleep(250)
  }
  // Up once the fast-forward is over: the top bar's "Starting up… N%" clears and the speed shows
  const booted = async () => {
    const t0 = Date.now()
    while (Date.now() - t0 < BOOT_MS && !(await evaluate(`document.getElementById('webspeed').textContent`))) await sleep(500)
    return (Date.now() - t0) / 1000
  }
  const speed = async () => {
    await sleep(3000)
    return parseInt(await evaluate(`document.getElementById('webspeed').textContent`)) || 0
  }
  const screenText = () =>
    evaluate(`fetch('/screen').then((r) => r.json()).then((s) => s.rows.map((row) => row.map((c) => String.fromCharCode(c[0])).join('')).join('\\n'))`)
  const stored = (key) =>
    evaluate(`new Promise((resolve) => {
      const open = indexedDB.open('bve9100', 1)
      open.onsuccess = () => {
        const get = open.result.transaction('files').objectStore('files').get('${key}')
        get.onsuccess = () => { open.result.close(); resolve(get.result?.length ?? 0) }
      }
    })`)

  await send('Page.enable')
  await send('Runtime.enable')
  await send('Emulation.setFocusEmulationEnabled', { enabled: true })

  await start('Start emulator')
  check(requests.includes('/emulator/a.img.gz') && requests.includes('/emulator/sram.bin.gz'), 'the first visit downloads the floppies and SRAM')
  const secs = await booted()
  check(secs * 1000 < BOOT_MS, `the machine boots (${Math.round(secs)} s)`)
  const first = await speed()
  check(first >= MIN_SPEED, `the machine runs at ${first}% of real time`)

  const decks = await evaluate(`fetch('/devices').then((r) => r.json()).then((d) => d.filter((x) => x.mode === 'stop').map((x) => x.port).join(''))`)
  check(decks === 'ABCD', `decks A-D are assigned and stopped (${decks || 'none'})`)

  // A key from the keyboard reaches the software: F1 (SOURCE) changes the screen
  const before = await screenText()
  await press('F1', 'F1', 112)
  await sleep(1500)
  check((await screenText()) !== before, 'a key changes the screen')

  let saved = 0
  for (let i = 0; i < SAVE_POLLS && !saved; i++) {
    await sleep(500)
    saved = await stored('sram.bin')
  }
  check(saved > 0, `the page saved SRAM (${saved} bytes) and floppy A (${await stored('a.img')} bytes)`)

  await evaluate(`${button('Stop')}.click()`)
  await sleep(4000)
  check((await evaluate(`!!${button('Continue')} && !document.getElementById('landing').hidden`)) === true, 'Stop returns to the startup page, offering Continue')

  requests.length = 0
  await start('Continue')
  check(requests.some((r) => r.startsWith('/emulator/bve9100.js')) && !requests.includes('/emulator/a.img.gz') && !requests.includes('/emulator/sram.bin.gz'),
    'a second visit runs from the saved machine')
  await booted()
  const again = await speed()
  check(again >= MIN_SPEED, `it runs at ${again}% of real time`)
} finally {
  chrome.kill()
  server.close()
  await sleep(500)
  rmSync(profile, { recursive: true, force: true })
}
process.exit(failed ? 1 : 0)
