# Development notes

Technical background for the BVE-9100 emulator. The user guide is the top-level `README.md`.

No hardware, schematics or ROM dump were available: the machine model was reconstructed by decompiling
the firmware with Ghidra (the kernel ships with a full symbol table) and from Sony documentation found
online. See `docs/hardware.md` for the reconstructed memory map and `docs/firmware.md` for the file formats.

## Status

* Boots `RMS.SYS` -> SRMS kernel -> `STRTUP.SYS` ("Kernel ( SRMS V2.32 ) Started", module list) -> STRAPL
  application switcher (large-font "BVE-9100 MULTITASK OPERATING SYSTEM" title and logo) -> the editing
  application comes up with its **edit page** (recorder R, players P1-P4, AX1/AX2, BLK, edit number, CUT) after
  about 2 minutes of emulated time (a few seconds of wall time).
* Emulated: 68030 + PMMU (Musashi, patched for TT0/TT1), DRAM/SRAM, 2 x 8254, 2 x 8259A, 8251 console,
  custom DMA controller, WD177x-style FDC with raw 1.44 MB images, RTC, EEPROM, LEDs/DIP switches,
  DSC-63 text VRAM + keyboard/printer mailboxes, SPF board (field interrupt, GPI timers), SIO-4 RS-232
  mailboxes, WD33C93 SCSI stub (no HDD).
* Emulated as stand-ins: IF-508 network board presence (required by this software edition), WD33C93 with no
  disk attached. The terminal view decodes the double-width title and shows other graphics cells as `#`/`%`.
* The kernel's idle loop is skipped: while the CPU sits in `__sysstp`'s NOP loop the devices are ticked straight to
  the next interrupt (`--no-idle-skip` runs it for real). This makes the boot about twice as fast in wall time
  without changing emulated time.
* Keyboard: wire codes reach the application and act on the edit page (player select keys verified);
  `keymap.txt` maps host keys to wire codes (full table: `docs/keyboard-codes.md`). `--tty` gives a live terminal
  view with keyboard input. The application's key-assignment table is loaded with the factory defaults by the
  emulator (`--no-keyassign` to disable), as a fresh machine's INIT MENU would.
* RS-422 interface boards with virtual VTRs and a switcher (`src/devices/rs422.cpp`, `idc_peers.cpp`,
  protocol in `docs/rs422-idc.md`): timecode, transport, GO TO, jog/shuttle, PREVIEW and recorded edits work;
  the switcher only answers status polls.
* Not yet: the DSC-63 character-generator ROM (the browser UI draws the text fonts and the boot title with system
  fonts, and the SONY wallpaper and BVE-9100 wordmark from vector outlines made by `tools/gen_cg.py`, see
  `docs/hardware.md`), switcher commands, hard disk.

## Build and command-line runs

```sh
brew install lhasa cmake          # lha is needed to unpack the Sony installer files
./tools/unpack_ref.sh             # unzips ref/*.ZIP into work/, builds work/sys1.img + work/sys2.img, raw modules in work/raw
cmake -B build && cmake --build build -j
./build/bve9100 --boot work/raw/rms.abs --disk-a work/sys1.img --disk-b work/sys2.img --cycles 2600000000
# browser front-end: live screen with proper fonts and a clickable BKE-9400A keyboard
./run_ui.sh                      # boots in turbo mode (~10 s), then runs in real time at http://127.0.0.1:8090/
# terminal front-end (live screen, keys from the terminal, Ctrl-] quits):
./build/bve9100 --boot work/raw/rms.abs --disk-a work/sys1.img --disk-b work/sys2.img --sram work/sram.bin --tty
```

The browser page (`ui/index.html`, served by the emulator's built-in HTTP server) polls `/screen` ten times a
second, draws the 80x29 display with text, line-drawing and double-width fonts, and shows the BKE-9400A keyboard
transcribed from the photos in `ref/`: click a key (or type on your keyboard) to send its wire code; SHIFT/CTRL
arm the next key (each key/modifier combination has its own wire code, taken from the factory key-assignment
table - hover a key to see them); the raw-bytes field sends anything.

The emulated system writes to the floppy images, so keep the generated `work/sys*.img` as masters and copy them
for each run if you want a clean boot.

The run stops after the given number of 20 MHz cycles (75 s emulated here, a few seconds of wall time)
and prints the DSC-63 text screen. Useful options: `-vv` device log, `--profile` PC histogram with
kernel symbol names (`--syms work/raw/rmsbdy.abs`), `--tp HEX` tracepoints, `--trace-from HEX`,
`--screen 500` to redraw the screen every 500 ms of emulated time, `--vram-dump FILE`.

## Layout

```
src/            emulator (bus, machine, CPU glue, devices/, httpd for the browser UI, tty front-end)
ui/             browser front-end (screen renderer + on-screen keyboard)
vendor/musashi  68k core (MIT), m68kmmu.h/m68kcpu.h patched for 68030 transparent translation
tools/          absfile.py (.abs parser/extractor), mkfat12.py (disk image builder), unpack_ref.sh
analysis/       Ghidra headless scripts, decompiled C of kernel/boot/startup/diag (gzip), symbol tables, I/O maps
docs/           hardware.md (memory map), firmware.md, research/ (brochure, user's guide OCR, protocol PDFs)
ref/            original Sony installer archives (input)
work/           generated, ignored by git
```

## Re-running the decompilation

With Ghidra installed at `$GHIDRA_HOME` (default `~/ghidra`): `analysis/ghidra/batch_ghidra.sh` writes
`work/ghidra/out/<module>.{c,asm,funcs}`. The checked-in `analysis/decomp/*.c.gz` are those outputs.

## Virtual VTRs and switcher

The emulator fits RS-422 interface boards with virtual devices: VTRs on ports A-D and a video switcher on
port E (`--device P=KIND[:TYPE]` to change, `--no-devices` to remove). Assign them once in the editor:
INIT, F3 (ASSIGN), F7, F4 (DFLT ALL), YES; this puts R on A and P1-P3 on B-D and is kept in SRAM. The reel
lines then show timecode and transport state; PLAY, STOP, STILL, FF, REW, JOG/SHTL and GO TO (select deck,
type timecode, SET IN, GO TO) work. PREVIEW, CTRL+REC (single edit) and AUTO REC run complete edits: the decks cue, roll in sync, the recorder punches in and out, and ALL STOP aborts. The search dial in the UI can be dragged (or scrolled) for JOG, SHTL and DMC. `GET /devices`
shows device state and `POST /devices/<port>` (body `play`, `stop`, `cue <frame>`, ...) drives a device from
outside; this is where video players or OBS will connect. Protocol notes: `docs/rs422-idc.md`.

