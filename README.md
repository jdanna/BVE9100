# BVE-9100 emulator

Runs the original software of the **Sony BVE-9100** in your browser, with an on-screen
Sony keyboard and virtual tape decks and switcher attached. Live at **[bve.retrograde.tools](https://bve.retrograde.tools)**.

![The emulator: editor screen above, BKE-9400A keyboard below](docs/screenshot.png)

## Setup (once)

You need macOS or Linux, a C++ compiler and Python 3.

```sh
brew install lhasa cmake                 # macOS
sudo apt install lhasa cmake g++ unzip   # Linux (Debian/Ubuntu)
./tools/unpack_ref.sh          # unpacks the Sony installer disks from ref/ into work/
npm run build:desktop          # or: cmake -B build && cmake --build build -j
```

## Start

```sh
npm start              # rebuilds if the source changed, then runs ./run_ui.sh
```

`./run_ui.sh` on its own starts the last build. Your browser opens <http://127.0.0.1:8090/>. The machine boots in fast-forward for about ten seconds and
then runs in real time on the edit page. Stop it with Ctrl-C in the terminal; the editor's memory (setup,
device assignment, EDL) is kept in `work/run/sram.bin` for next time. Delete that file to start from a new
machine: decks assigned, EDL DISPLAY and DISP RECORDED EDIT on, as in the web version. Use `PORT=9000 npm start` to pick another port, and
`npm start -- --no-swmem` (any emulator option after `--`) to pass options through.

## The decks

Four virtual VTRs are connected to ports A-D and a video switcher to port E. A new machine has them assigned
already. On a machine from an older `work/run/sram.bin`, or after a factory reset, tell the editor about them once:

1. Press **INIT**, then **F3** (ASSIGN).
2. Press **F7** to show the second menu page, then **F4** (DFLT ALL) and **YES**.
3. Press **RET** until you are back on the edit page.

The reel lines now show R on port A and P1-P3 on ports B-D, each with timecode and transport state. Every
virtual tape starts at 01:00:00:00.

## Using the keyboard

* Click the keys on the on-screen keyboard. **SHIFT** and **CTRL** arm the next key: the small top legend
  on a key is its SHIFT function, the bottom legend its CTRL function. Hover a key to see what it sends.
* Your computer keyboard works too: letters, digits and Space act like the same position on the Sony keyboard
  (for example A = P1, V = PLAY, Z = REW, X = FF, C = STILL, Space = ALL STOP; P [ ] = CUT, DISS, WIPE).
  F1-F8 are F1-F8, F9-F12 are PF1-PF4, Esc is INIT, Tab is PREVIEW, Enter is ENTER, Backspace and Home are
  RET, Page Up and Page Down are YES, Delete is NO, = is BS, ` is INIT, ' is KEY,
  numeric-pad . is 00. Holding Shift or Ctrl gives
  a key's top (SHIFT) or bottom (CTRL) legend, as the keyboard's own SHIFT and CTRL keys do (Shift+3 = MOD EDL,
  Ctrl+3 = STOR ED, Ctrl+E = REC, Shift+Tab = MULTI-PVW).
* The **F keys / Source** switch in the top bar (or Shift+Space) turns Esc and F1-F9 into the source keys:
  Esc = R, F1-F6 = P1-P6, F7/F8 = AUX1/AUX2, F9 = BLACK; Shift then gives the F keys (and INIT on Esc). In
  F-key mode it is the other way round: Shift+Esc/F1-F9 are the source keys. The on-screen key goes down while
  you hold its key.
* The lamps on the keyboard follow the editor: the selected source (R, P1-P6, AUX, BLACK), the audio
  channels A1-A4 and the dial mode.
* The search dial: press **JOG**, **SHTL** or **DMC**, then drag the dial around (or scroll over it). In
  shuttle, the further you turn from where you started, the faster the deck winds; turn back to stop. The
  SEARCH LED strip above the dial shows how far it is turned, and the REVERSE / FORWARD lamps the direction.

## Screen colours

**Palette** (top right) opens the editor's 16-colour VDU palette, the one INIT > LAYOUT > CHG DATA > PALETTE
sets. Pick colours, or load a preset (the Sony factory palette or one of Tuesday's colour schemes), and the screen
previews them. **Inject into SRAM** writes them into the editor's setup database and the editor uses them
straight away; stop the emulator with Ctrl-C to keep them in `work/run/sram.bin`. Colour 0 draws the frame lines
and colour 8 is the screen background; which colour each part of the screen uses is set under INIT > LAYOUT.

## Editing

* **Transport:** select a deck (R or P1-P6), then PLAY, STOP, STILL, FF, REW, SLOW, or the dial.
* **Go to a timecode:** select the deck, type the timecode on the number keys, press **SET IN**, then **GO TO**.
* **Mark an edit:** select a deck and press **SET IN** / **SET OUT** (or type a timecode first), choose CUT,
  DISS or WIPE, then **CTRL+3** (STOR ED) to store it in the EDL.
* **Preview** an edit with **PREVIEW**; record a single edit with **CTRL+REC**; run the whole EDL with
  **AUTO REC**. **ALL STOP** aborts.
* To recall an edit from the EDL, type its number and press **BS**.

* **Switcher:** CUT, DISS and WIPE edits and the source keys drive the virtual switcher on port E, which works
  like a Sony BVS-3000-series switcher. SWMEM (SHIFT + MOD EVENT) opens its E-FILE register menu (bank ME1,
  registers 0-99).

## OBS Studio

`obs/bve9100_obs.py` turns OBS into the edit suite's video side. Media files act as the tape decks and follow the
editor's transports, pre-rolls and edits frame by frame. The editor's switcher drives OBS's program: cuts,
dissolves and wipes, with the editor's transition rate. Add it under **Tools > Scripts**, pick a file for each
deck and press **Create deck scenes**. That also makes empty **BVE AX1**, **BVE AX2** and **BVE BLACK** scenes for
the editor's AUX1, AUX2 and BLACK sources (crosspoints 8-10); put anything you like in them.

It can also record the record tape. With **Record edits** ticked in the emulator's **OBS TAPE** toolbar, OBS
records every edit the editor makes as a segment of the tape: its timecodes and its tracks, so an audio-only
insert only replaces audio. **Export** conforms the segments into one file with ffmpeg: ProRes or H.264, with
timecode, A1-A4 as separate tracks or mixed. **Undo** removes the last segment. Details: [docs/obs.md](docs/obs.md).

## GPI and MAXINE!

The editor's four GPI outputs light the **GPI** lamps in the top bar. Fire them from GPI events in an edit or with
TEST FIRE in GPI mode. Each pulse is also sent as a `gpi N` UDP datagram to the MAXINE! emulator (MaxineStudio) on
127.0.0.1:49100, which closes its GPI input N. GPI-0 is MAXINE input 1, GPI-1 is input 2, and so on. Options:

* `--gpi-udp HOST:PORT` sends to another target.
* `--gpi-base N` changes the first input number.
* `--no-gpi-udp` turns it off.

## Driving the decks from outside

The emulator serves a small HTTP interface on the same port, so scripts or other programs can watch and
control the virtual devices:

```sh
curl http://127.0.0.1:8090/devices                               # state of every deck and the switcher
curl -X POST -d play  http://127.0.0.1:8090/devices/B            # put P1 (port B) in play
curl -X POST -d "cue 108000" http://127.0.0.1:8090/devices/B     # cue to frame 108000 (01:00:00:00)
curl -X POST -d "pgm 3" http://127.0.0.1:8090/devices/E          # switcher: crosspoint 3 to program
curl -X POST -d +20   http://127.0.0.1:8090/dial                 # turn the search dial
curl http://127.0.0.1:8090/palette                               # VDU colour palette (16 x 6-bit r g b)
curl -X POST -d "63 63 63  63 0 0 ..." http://127.0.0.1:8090/palette   # inject 48 values into SRAM
```

## Web version

**Try it: [bve.retrograde.tools](https://bve.retrograde.tools)**, nothing to install.

The emulator also builds to WebAssembly and runs as a static site. Each browser keeps its own machine (setup, EDL,
palette) in IndexedDB; **Reset…** in the top bar goes back to the default one.

To build it you need Node and [Emscripten](https://emscripten.org/) in `~/emsdk`:

```sh
git clone --depth 1 https://github.com/emscripten-core/emsdk.git ~/emsdk
(cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest)
```

```sh
npm run snapshot:web   # default machine: sampled from the emulator running on port 8090
npm run build:web      # builds dist-web/ (BVE_SKIP_EMULATOR=1 reuses the last emulator build)
npm run preview:web    # serves it at http://127.0.0.1:8091/
npm run test:web       # boots it in headless Chrome and checks it
npm run deploy:web     # builds and publishes to Cloudflare Pages (npx wrangler login first)
```

The Pages project is `bve9100-emulator`. The landing page is `web/landing.html`, and its changelog is
`web/CHANGELOG.md`.

## More

* `docs/development.md`: how the emulator works, command-line options and the project layout.
* `docs/hardware.md`, `docs/rs422-idc.md`, `docs/keyboard-codes.md`: reverse-engineered hardware and protocols.
