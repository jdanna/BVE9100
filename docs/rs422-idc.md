# RS-422 interface boards and the IDC protocol

The BVE-9100 does not speak Sony 9-pin to the decks itself. Each RS-422 interface board (SIO-3 BKE-9002,
IF-263, IF-328 BKE-9012/9014, IF-342) carries an **IDC** ("intelligent device controller") sub-CPU that
runs the line protocol to the device. The 68030 talks to the IDC through dual-port RAM using short
messages. Everything below was worked out from the SRMS kernel (`supsio3`, `ptcmdidc`, `ptprdcmd`,
`intrd`/`intred`, `i_semsg`), STRAPL, MAIN, the DIAG board test, and by booting the emulator with
virtual boards and logging what the application sends. Items marked *(guess)* work with the
application but are not confirmed against hardware.

## Board window

Option slot n (0-6) is at `0x0FFF9000 + n*0x1000`. Each 4 KB window is 4 channels of 1 KB. Port letters on
the DEVICE ASSIGNMENT page follow the kernel numbering: port A = slot 0 channel 0, B = slot 0 channel 1,
E = slot 1 channel 0, and so on (28 ports, A-Z then a, b).

| Offset (per channel) | Use |
|---|---|
| `+000` | RX: the latest reply message. The kernel posts the first `+3B8` bytes of it to the requesting task |
| `+120..1FF` | replies to the periodic slots; `+3B0 + 2k` holds the offset (from `+120`) of slot k's reply, 0 or FFFF = none |
| `+200` | TX: one host message (one-shot command); writing its length to `+398` sends it |
| `+300 + 0x20*k` | periodic command slot k = 1..3; length at `+398 + 2k`, period at `+390 + 2k` (high byte = fields) |
| `+380` (w) | host command: 3 = reset channel, 2 = acknowledge error |
| `+382` (w) | line format, kernel writes 0x0A77 |
| `+384` / `+386` (w) | interrupt vectors: reply ready (0x60 + 2·port) / error or time-out (0x61 + 2·port) |
| `+3A0` (w) | channel status: bit 15 present, bits 8-9 ready; `+3A1` = error code after a failure |
| `+3B8` (w) | length of the RX message |
| board `+3C0` (w) | 1 = start, 3 = reset |
| board `+3FC` (w) | firmware version |
| board `+3FE` (w) | board ID: `0x2xxx` = RS-422 board, bits 0-3 = channels fitted (`0x100F` = SIO-4 RS-232) |

## Messages

`[type, len, cmd, sub, data...]`. Type 0x02 is a normal message. The application's message constants carry
no checksum (the kernel copies one extra byte). Replies use the same layout with the command byte plus
0x10 (`09 00` → `19 00`, `61 20` → `71 20`).

In the RX area the first byte is a result code that STRAPL dispatches on: 4 = reply to the pending
command, 2 = data message for the device-class handler, 5 / 0xFF = error.

### IDC housekeeping (any device class)

| Message | Meaning |
|---|---|
| `09 00` → `19 00 TT TT` | status poll (periodic slot 3); TT TT = IDC device type, see below |
| `09 01` → `19 01 vv vv` | IDC version (DIAG) |
| `08 00` → `18 00 xx xx` | device state poll (slot 3, every 31 fields once a device is found) |
| `01 00 00` | mode set at start-up *(reply: ack)* |
| `42 xx yy`, `23 00`, `43 20 ...` | parameter set-up; `43 20` downloads the VTR model table from `VTRC30.IDT` / `VTRC25.IDT` |

### IDC device type word (reply to `09 00`)

| Type | Device class shown on DEVICE ASSIGNMENT |
|---|---|
| `01xx` | VTR |
| `02xx` | ATR |
| `03xx` | DISP |
| `1xxx` | SW'ER (video switcher) |
| `2xxx` | GPI |
| `3xxx` | DME |
| `4xxx` | CHANGER |
| `5xxx` | COLOR |
| `Exxx` | DISK |

### VTR

Status sense runs in periodic slot 2 every field: `61 20 1F` (data byte = mask of blocks wanted). Reply:

```
02 len 71 20 mask
  bit0: tc-type (bit7 set = no '.' after TC), frame count (4 bytes, big-endian), 4 more bytes
  bit1: s0, s1   s0 = 0xC0 when the IDC is talking to the deck and the deck is in remote
                 (0x80 alone shows LOCL / TOUT, otherwise XXXX); s1 = transport:
                 04 STOP, 0C FF, 0D REW, 10 forward (var/jog), 11 reverse, 12 STILL
  bit2: 2 bytes (unknown)
  bit3: servo group, play code, lock, extra (02 12 20 00 shows PLAY)
  bit4: 1 byte (unknown)
```

Transport commands seen from the keyboard:

| Key | Message |
|---|---|
| STOP | `20 00` |
| PLAY | `20 01` |
| STILL | `20 03` |
| FF | `20 06` |
| REW | `20 07` |

| SHTL | `20 12` (dial speed messages not captured yet) |
| JOG | `20 10` |
| GO TO (deck selected, timecode typed, SET IN, GO TO) | `22 05` + target as a 4-byte frame count |
| SLOW | `20 11 00 14` |

`61 10 01` is polled too; the emulator answers with a `71 10` data message (24 bytes, contents guessed).

Variable speed and search (data = `[direction 0x80 = reverse, magnitude]`):

| Command | Meaning |
|---|---|
| `20 10 dd nn` | JOG mode |
| `20 20 dd nn` | jog step of nn frames (sent per dial movement) |
| `20 11 dd nn` | variable speed (SLOW, DMC dial mode) |
| `20 12 dd nn` | shuttle; nn grows with the dial position |

Speeds follow the Sony 9-pin curve 10^(nn/32 - 2) x play speed (an assumption; nn = 64 is play speed).

#### Edit sequences (PREVIEW, CTRL+REC single edit, AUTO REC)

The edit is run by the IDCs on a shared timeline; AUTOX drives it and waits on the status reply's servo
block, whose first byte (low nibble) is the sequencer phase:

| Phase / code | Meaning |
|---|---|
| `02` + `08` | cueing |
| `02` + `10` | parked at the cue point |
| `03 14` | parked and locked to the start timeline |
| `04 14` | rolling in sync, and after `23 01` "sequence ended" |
| `00` | idle |
| bit 3 | error |

Message flow for one edit (R recorder, P1 player):

1. Set-up per deck: `40 00 <now>`, `21 08`, `42 04 ..`, `42 06 <bit7 = record armed>`, `40 10 <pre-roll 150>`,
   `40 11`, `40 01 <rec in> <rec in>`, `40 02 <rec out>`, `40 03 <rec out + post-roll>`,
   `40 05 <this deck's in> <in>`, `42 01`, `42 03 xx`.
2. Recorder only: `41 01 <frame> <action>` timed events (10 = ?, 12 = record on at the in point, 11 = record
   off at the out point).
3. `22 14 00 08`: cue to in point minus pre-roll; AUTOX waits for phase 2 / code 10 ("parked").
4. `60 03`: data reply `70 03` + 4 bytes: the deck's position on the edit timeline as a frame count. The
   timeline is counted in the recorder's timecode, so a deck at tape position p stands at
   p - (its in point, `40 05`) + (recorder in, `40 01`); parked decks all report the same value. Answering the
   raw tape position only works while source and recorder in points are equal: otherwise AUTOX counts the
   timeline in the player's timecode and waits for the recorder to roll that much later. Without a data reply
   AUTOX reports CANNOT EXECUTE. The `60 xx` / `70 xx` reply sizes come from a table in CMNDB (0xD002E3A6).
5. `40 00 <start timeline>`: the same value for every deck, the timeline position minus a few frames. The IDC
   counts it up one per frame and rolls the deck when it reaches the deck's parked timeline position (so all
   decks roll together); AUTOX waits for phase 3 / code 14 first.
6. Rolling: phase 4 / code 14. The editor's own clock runs the edit (one tick per frame, see below) and sends
   `23 01` at the end of the post-roll; the deck stops and keeps reporting phase 4 / 14 ("ended").
7. `23 00` releases the decks (idle); `60 02` / `60 04` are read; STOP follows. `23 00` during a roll (ALL STOP)
   aborts the sequence.

The editor's timing comes from the SPF reference interrupt (vector 0x5B): it must arrive once per frame
(29.97 Hz), not once per field. With a per-field interrupt the editor's clock runs twice as fast and every
edit ends early with CANNOT EXECUTE / NOT READY.

#### Search dial

The BKE-9400A dial is not a key code. The keyboard sub-CPU keeps a counter in the DSC-63 keyboard mailbox RAM;
the kernel's `dialstat()` returns `(word at 0x0D01CA00 + p) << 16 | (word at 0x0D01CA02 + p)` with
p = word at 0x0D01CBE0. The high word's top bits give the dial mode (0x2000 SHTL, 0x4000 JOG, 0x8000 DMC, set
by those keys); MANCNT only uses the counter when the mode matches. The emulator keeps the counter
(`POST /dial` with signed counts, `--dial CYCLE:COUNTS`), and the UI's dial sends 120 counts per turn.

### Switcher

With a SW'ER IDC on the port chosen as switcher, slot 1 polls the switcher every 3 fields: `64 80` for
type words with bit 0x40 clear, `6C 80 00 83` with it set. Both are answered by
`7C 80 00 flags status(2) [bit0: 4 bytes] [bit1: 2 bytes] [bit7: 2 bytes]`; status 0 = no fault. At
start-up the editor also sends `42 00 00`, `4F 44 0F FF`, `24 1F 00 FF 00 FF 00 FF` and `21 40 00 02 00 FF FF`.
Every other switcher command is answered with its command byte + 0x10.

Switcher commands (data after the `type len` header):

| Command | Meaning |
|---|---|
| `21 40 00 02 00 FF xx` | Live crosspoint: put source `xx` on program now (source keys with MONITOR SELECT = PGM; `FF` = none) |
| `41 00 00 02 07` | Clear the event list of the next preview / edit |
| `41 40 FL 02 <frame:4> t FF A FF B FF` | Crosspoint event at timeline frame: `t` 0 = cut to `A`; 4 = mix, 8 = wipe, `A` on program, `B` on preset |
| `41 50 FL 02 <frame:4> 04 <rate:2>` | Start the transition at timeline frame, rate in fields |
| `41 60 FL 02 <frame:4> hi lo FF FF` | Wipe pattern |
| `40 00 <frame:4>` | Start the timeline at this frame (the same start the VTRs get) |
| `24 10 00 03 00 01 00 01` / `24 18 ...` | At the roll / at the end of the preview or edit |

Crosspoint numbers are the editor's XPT ASSIGN values (INIT > ASSIGN, VIDEO EFF); the defaults are R = 1, P1 = 2,
P2 = 3, P3 = 4. AX1, AX2 and BLK, on the assignment's second page, start unassigned and are then sent as `FF`. The
emulator sets them to 8, 9 and 10 at boot while they are unassigned. They are kept in the battery SRAM as 5-byte
records (video EFF, video MON, audio EFF, audio MON, -) for P1-P12, AX1, AX2, BLK and EFF, AX1's at 0x00806156,
with `FF` = unassigned and no checksum. A DISS from
P1 to R at the in point gives `41 40` (cut to 2) and `41 40` with `t` = 4, `A` = 2, `B` = 1 plus `41 50` with the
rate, all at the in-point frame. SWMEM's SET EFF only stores a switcher memory number in the edit; the recall
message for it has not been seen yet.

#### SWMEM and the switcher library

SWMEM (SHIFT + MOD EVENT) does not depend on what the switcher answers. The editor drives each switcher model
through a "switcher dialogue" library at `0xC4000000` (data at `0xD4000000`), and the disks only carry SWDLG2,
a stub whose descriptor is all zero, so SWMEM answers UNSUPPORTED FUNCTION. The manual's switcher list (GVG
100/200/300/1680, Kadenza, A84, HDS-1000, Sony BVS-3000 series) names libraries that are not on the disks. The
emulator fills the descriptor in after boot for a Sony BVS-3000-style switcher: bank ME1, E-FILE registers 0-99,
F1-F8 labels on (`hle_switcher_profile()` in `src/machine.cpp` lists the fields). It also sets the SWER LOCAL
modes PREVIEW MODE = PGM and MONITOR SELECT = PGM (RAM only; this edition's SETUP has no switcher group), without
which the editor sends the switcher nothing. `--no-swmem` leaves the stub alone.

## Emulator

`src/devices/rs422.cpp` implements the board (window, one-shot and periodic slots, vectors);
`src/devices/idc_peers.cpp` holds the IDC behaviour and the virtual devices. By default the emulator fits
VTRs on ports A-D and a switcher on port E (`--device P=KIND[:TYPE]`, `--no-devices`). After the first
boot with devices, assign them once: INIT, F3 (ASSIGN), F7, F4 (DFLT ALL), YES. This gives R = A,
P1-P3 = B-D and is kept in SRAM.

HTTP: `GET /devices` returns each device's state. `POST /devices/<port>` with a body of `play`, `stop`,
`ff`, `rew`, `still`, `rec`, `eject`, `load`, `goto <frame>` (VTR) or `pgm N`, `pst N`, `cut`, `auto [frames]`,
`trans cut|mix|wipe`, `rate N` (switcher) drives a device from outside the editor. The virtual switcher runs the
event list on its timeline and counts every take (`take.n` in `/devices`, with type, from, to and length);
`obs/bve9100_obs.py` follows it, see `docs/obs.md`.
