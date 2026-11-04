# BVE-9100 hardware, as reconstructed from firmware

Everything below was derived from the firmware on the installer disks in `ref/` (SRMS kernel
`RMSBDY.SYS` with its symbol table, the `RMS.SYS` boot loader, `STRTUP.SYS`, and the BVE-9000K
maintenance disk `DIAG.SYS`/`MAINTE.OBJ`), cross-checked against the Sony brochure and the German
BZE-9101/9102 user's guide (`docs/research/`). **No schematics or ROM dump were available.**
Items marked *(assumed)* are emulator choices that the firmware tolerates but that are not confirmed.

## Boards

| Board | Role | Sales code |
|---|---|---|
| **CPU-88** | Main CPU: MC68030 @ 20 MHz, 4 MB DRAM (16 MB "BDRAM" option), 512 KB battery-backed SRAM (1 MB with DSK-6), 2 KB EEPROM, 2 x 8254 timers, 2 x 8259A interrupt controllers, 8251 debug USART, custom DMA controller, WD177x-class floppy controller ("MFD block"), WD33C93 SCSI (BKE-9107 HDD), RTC, LEDs, DIP switches, monitor EPROM "9100M0" (V1.11 last) | – |
| **DSC-63** | Colour/mono VDU text display (80x29, 16 colours, 4 CG banks), keyboard interface, RS-232C printer interface; both ports are mailbox-driven sub-processors | – |
| **SPF-1/2/2A/3/4/4A/5** | Reference video (NTSC/PAL), colour framing, field interrupt, 4 GPI outputs (two 8254s used as pulse timers) | – |
| **SIO-3 / IF-51 / IF-263 / IF-328 / IF-342** | RS-422 device interfaces, 4 channels each, with an "IDC" sub-CPU (68000 code downloaded from disk, e.g. `MIXER.IDC`) | BKE-9002 / BKE-9004A / BKE-9012 / BKE-9014 |
| **SIO-4** | 4 x RS-232C (ptp, tty, smc, aux), mailbox-driven | BKE-9003 |
| **IF-508** | Ethernet board (MIPS ECOFF firmware `IF508A/B.OBJ`), talks to an SGI Indy ("simplefile" TCP 9100) | – |
| **DSK-6** | extra SRAM window 0x880000-0x8FFFFF | – |

## Memory map (physical)

| Range | Contents | Source |
|---|---|---|
| `0000_0000-0000_FFFF` | Monitor EPROM (not dumped; emulator uses a stub) | reset vector |
| `0080_0000-0087_FFFF` | SRAM 512 KB (battery backed; `!BUPDB` at 0x804000, 0x7C000 bytes) | DIAG, BOOT.MAP |
| `0088_0000-008F_FFFF` | SRAM with DSK-6 (kernel probes 0x880000 with 0x5AA5) | DIAG, `hdinit` |
| `0100_0000-013F_FFFF` | DRAM 4 MB. Vector table at 0x1000000 (VBR), kernel text 0x1010000, data 0x1040500, boot loader at 0x13E8000 (text) / 0x13ED000 (data), initial SP 0x13FFFFE | `.abs` headers, kernel |
| `0100_0000-01FF_FFFF` | DRAM 16 MB with BDRAM | DIAG |
| `0D00_0000-0D00_FFFF` | DSC-63 text VRAM (16 K cells x 4 bytes) | `fillvram` |
| `0D01_0000-0D01_BFFF` | DSC-63 second plane ("pop-up") | `fillppup` |
| `0D01_C800-0D01_CFFF` | DSC-63 registers (see below) | |
| `0F00_0000` | CPU-88 reset strobes (+1 = SIO reset) | `sioinit` |
| `0F00_1000` | 8251A debug console (+0 data, +1 mode/cmd/status) | `sioinit` |
| `0F00_2000` | 8254 PIT0 (+0..2 counters, +3 ctl). ctr0 mode2 65535, ctr1 mode2 40000 = system tick, ctr2 mode2 | `ini_pit0` |
| `0F00_3000` | 8259A ICU1, vectors 0x58-0x5F | `ini_icu` |
| `0F00_4000` | 8259A ICU0, vectors 0x50-0x57 | `ini_icu` |
| `0F00_5000` | 8254 PIT1 (ctr1 mode3, ctr2 = 52 -> 8 MHz/52/16 = 9600 baud for the 8251) | `ini_pit1` |
| `0F00_6000` | RTC, 16 nibble registers (RP5C01 / MSM6242 layout: sec1,sec10,min1,min10,hr1,hr10,wday,day1,day10,mon1,mon10,yr1,yr10,ctlD,ctlE,ctlF) | DIAG |
| `0F00_7000` | FDC (+0 status/cmd, +1 track, +2 sector, +3 data). WD177x command set plus a vendor command 0xFE + data byte (0x40 3.5", 0x60 8") issued before each access | boot loader, `fdtsk` |
| `0F00_8000` | DMA controller: +0 channel select, +2 count-1 (w), +4 address lo16 (w), +7 address bits 23-16, +8 command (w; bit2 = disable; init 0x0230), +A status (TC bits), +B mode (0x44 dev->mem, 0x48 mem->dev), +E mask, +F request. Channel 3 = floppy. | `dmahset`, `fdma_*` |
| `0F00_9000` | WD33C93 SCSI (+0 address, +1 data) | `CDBWrite` |
| `0F00_A000-0F00_A7FF` | EEPROM 2 KB. 0x7F9 bits0-2 boot drive (2 = B, 4 = HDD, else A); 0x7FF bit0 serial bit-rate strap | `asgetdrv`, `rd_brbit` |
| `0F00_B000` | FDD select latch: bit0 drive, 0x80 3.5" / 0x90 8" select | `fdselect` |
| `0F00_B001` | FDD control: 0x40 side1, 0x20 head load, 0x10 clear disk-change, 0x08 precomp (trk>59), 0x04 motor, 0x02 2HD | `fdset_hea`, `clr_dchg` |
| `0F00_B002` | DIP switch (read). bit7=0 -> kernel installs its own bus/address error handlers ("RMS control vectors") | kernel `main` |
| `0F00_B003` | 8 LEDs (active low) / 7-segment; LED0 = idle indicator | `led0on`, `set7seg` |
| `0F00_B004` | FDD status (read, active low): bit1 = 0 -> disk changed | `chk_dchg` |
| `0F00_B006/7` | written 0x30 / 0x0F at kernel start (bus timing?) | kernel `main` |
| `0F00_C000` | DMA page latch: 0xC0 \| address bit 24 | `dmahset` |
| `0F00_D000` | SCSI reset | `hdinit` |
| `0F00_E000` | bus-error cause register, bit0 (read by the kernel's bus error handler) | `_main` |
| `0FF0_1000-0FF0_1FFF` | SPF: 8254 #1 at +0x001/3/5/7, 8254 #2 at +0x201/3/5/7, GPI latches +0xE03/+0xE07, status +0xFD1/+0xFD3, board ID +0xFD7 (hi nibble 1 = SPF-1/2, 3 = SPF-3, 4 = SPF-4 *(assumed)*) | `spfIdent`, DIAG |
| `0FF0_2000-0FF0_3FFF` | IF-508 Ethernet board: 8 KB dual-port RAM. +0000 status (bit3 = board ready, `if508rdy`), +0002 presence probe (`if508moun` writes 0x5555), +0010 host/board command word (0xFFFF after board boot, host acks with 0, commands are `cmd|0x8000`), +0008/+000C interrupt vectors of the two net channels (0x4C/0x4D), +19C0 netsio command, +19C4 netsio status (bit 0x1000 ready), +19C8..+19D4 baud/char/stop/parity, +19D8 netsio vector (0x4E), +19DC TX count, +1A00 TX data, +19E0 RX count, +1B00 RX data, +1FF8 firmware magic 0xFF050800 (set only once the MIPS firmware is running), +1FFC doorbell | `net_init`, `netsioini`, `nsio*` |
| `0FFF_1000` | SPF alternate decode (probe with 0x55/0xAA) | `spfIdent` |
| `0FFF_9000 + n*0x1000` (n=0..6) | Option slots BKE#0..#6 (chassis slots 11..5 from the photo in `ref/`), 4 KB each. Board ID word at +0x3FE: `(id & 0xF000) == 0x2000` = RS-422 interface board; the kernel assigns channel vectors 0x60+2n | `supsio3` |
| `0FFF_E000/E400/E800/EC00` | SIO-4 RS-232C channels ptp / tty / smc / aux (mailbox ports, 1 KB each) | `ttyinit` etc. |

### DSC-63 registers (offset from 0x0D000000)

* `1C980/82/84/86` (w): partition 0-3 start = (VRAM address - 0xD000000) >> 2; `1C990/92/94/96` (w): partition line counts (29 total). The application runs with partition 0 at 0x2440 (VRAM 0x9100).
* `1C9A0` (w) cursor address, `1C9A2` cursor shape/control, `1C9A4` misc; `1C9C0` (w) control (0x8000 display on; app writes 0xC001); `1C9C8` (r) bit0 busy.
* `1CA00-1CAFF` keyboard mailbox, `1CC00-1CCFF` printer mailbox, `1CFFE` (w) update strobe.
* Cell format: word0 = `0x5000 | CGbank<<8 | char`, word1 = `colour(0-15) | blink<<14 | reverse<<15`; 0x140 bytes per 80-column row.
* Colours (DIAG): 0 black .. 7 white plus DARK GREEN, ASH BLUE, VERMILION, PALE PINK, MOSS GREEN, LIGHT BLUE, BRIGHT YELLOW.

### Mailbox sub-processor ports (keyboard, printer, SIO-4 channels)

`+00` command (w; 6 = init, 1/2 = poll), `+02` param1, `+04` param2 (baud/format), `+0A` vectors (hi TX, lo RX), `+0C` vectors (hi "nop", lo reset), `+20` status (bit0 = sub-CPU idle), `+40` TX count then bytes at `+42`, `+E0` RX count then bytes at `+E2` (16 max). The sub-CPU interrupts the 68030 with the programmed vector numbers. The reset vector means "sub-CPU restarted" and must not be raised on command completion.

Keyboard protocol (BKE-9400A): host blocks are `82 <len> <cmd> <data...> <checksum>`; commands seen: 'W' (0x57)
mode set, 'R' (0x52) read a register ('Z' = keyboard type/version byte), 'K' (0x4B) key attribute table (pairs of
<type, code>). Keyboard answers `83` ACK, `84` NAK, `82 <byte>` for a register read. Key events are single wire
codes (one per physical key, no release code): a plain byte, or prefix 0x80 / 0x81 plus a byte, which the kernel
turns into 16-bit codes in planes 0x100 / 0x200 (0x8D selects 0x300). SHIFT and CTRL are resolved inside the
keyboard - every key/modifier combination has its own wire code; the host never sees a modifier. KTASK translates
the 16-bit wire code through a 768-entry key-assignment table (factory copy at CMNDB 0xD0096A44) into a function
code, and the KEY ASSIGN menu's name table (CMNDB 0xD0099544 / 0xD0097644, `tools/keynames.py`) names every
function. Typewriter keys send the ASCII code of their legend (digits 0x30-0x39; letters 0x61-0x7A for the main
legend, 0x41-0x5A for the SHIFT legend, ASCII control codes 0x01-0x1A for most CTRL legends; CTRL+digit = 0x9x),
the top row R, P1-P6, AUX1, AUX2, BLACK, A1-A4 is 0xF0-0xFD, INIT 0xE0, ENTER 0x0A, RET 0x0D, ALL STOP 0x20, and
the left block, F/PF keys, cursor keys and numeric-keypad symbols arrive with prefix 0x80 (F1-F8 = 80 18..1F,
PF1-4 = 80 13..16, YES/NO = 80 22/80 21, cursor 80 24..27, left block 80 0x / 80 4x / 80 8x for plain/SHIFT/CTRL).
Full list: `docs/keyboard-codes.md`. KTASK routes transport keys to MANCNT (task 0x18) and the rest to MAIN (0x16), MDISP-side tasks 0x19/0x33.

## Chassis and rear panel (from photos in `ref/`)

Card cage, left to right: 1 CPU-88, 2 (spare), 3 BKE#8 = IF-508 (two 7-segment digits and a DIP switch on
the front edge), 4 BKE#7, 5 BKE#6 = IF-328 (BKE-9012), 6 BKE#5 = IF-263 (BKE-9011), 7 BKE#4, 8 BKE#3 = SIO-3
(BKE-9002), 9-11 BKE#2..#0 = IF-328B (BKE-9014), 12 DSC-63, 13 SPF-3. Rear panel: per-board pairs of 9-pin
"9P"/"IF" ports labelled A-N, GPI/M-N, MIXER Q / SW'ER R, MON 1/MON 2, ports 1-4; REF VIDEO IN/OUT, FIELD REF,
MAIN KYBD (25-pin), GPI (25-pin), PRINTER (37-pin), COLOR VDU (9-pin), B/W VDU (BNC), G/I 1-2, AUX, PARALLEL FDU.

## Boot dependencies discovered while bringing the application up

* The BZE-9102 V3.04 disks in `ref/` are the **network ("Indy") edition**: the application switcher STRAPL
  issues a mode-set request to the kernel's netsio device (0x89) and waits forever if the IF-508 is absent, so
  the emulator always presents an IF-508 (`--no-if508` disables it). The board firmware itself is not run; the
  firmware magic at 0x0FF03FF8 is left clear so the application does not wait for Indy answers.
* STRAPL also asks the hard-disk server task (`hddos`) for a mount; `hddos` only answers after the WD33C93 has
  completed its reset and probe commands with interrupts (reset -> status 0x00, select -> 0x42 "no target").
* The kernel only runs its field-synchronous timer (`_tup2req`) when the SPF status register (0x0FF01E09)
  has bit 6 set (reference video present).
* The DSC-63 keyboard/printer mailbox "reset" vector must not be raised after each command (it means
  "sub-CPU rebooted" and makes the drivers re-initialise forever).

## Interrupts

Vector-driven: handlers are registered with `irqvct(vector, fn)`. Observed assignments:

| Vector | Source |
|---|---|
| 0x40-0x4B | SIO-4 RS-232 channels (ptp 40-42, smc 43-45, tty 46-48, aux 49-4B: rx/reset/tx) |
| 0x4C-0x4E | IF-508 net channels 0/1 and netsio |
| 0x50 | ICU0.0 DMA terminal count |
| 0x51 | ICU0.1 SCSI (hdkirq) |
| 0x52 | ICU0.2 FDC (fdirq) |
| 0x53 | ICU0.3 timer "tup3" (PIT0 ctr2 *(assumed)*) |
| 0x5A | ICU1.2 system tick "tup1" (PIT0 ctr1, 200 Hz) |
| 0x5B | ICU1.3 field interrupt from SPF (59.94/50 Hz) |
| 0x5D/0x5E/0x5F | ICU1.5/6/7 8251 console rx / break / spare |
| 0x60-0x9F | RS-422 interface channels 0-31 (pairs: data / end) |
| 0xA0-0xA3 | DSC-63 keyboard rx / reset / tx / nop |
| 0xA4-0xA6 | DSC-63 printer rx / reset / tx |
| TRAP #1-#7 | SRMS system calls (vectors 0x21-0x27 set by the kernel) |
| TRAP #14 | monitor debug hooks (`__settcb` etc.) - the emulator's stub just returns |

IPL levels are not known; the emulator uses ICU0 = 5, ICU1 = 4, DSC-63 = 3, SIO-4 = 2 *(assumed)*.

## MMU

The kernel enables the 68030 PMMU: TC = 0x80A07780 (1 KB pages, TIA=7 TIB=7 TIC=8), TT0 = 0x00038143
(0x00000000-0x03FFFFFF transparent for supervisor), TT1 = 0x0C038516 (0x0C000000-0x0FFFFFFF transparent),
one CRP per task. Application tasks are linked at logical 0x80000000 (text) / 0x90000000 (data); the shared
library CMNDB at 0xC0000000 / 0xD0000000.

## Boot sequence

1. Monitor ROM (missing) sets VBR, loads `RMS.SYS` from drive A to 0x13E8000 and jumps to text+0x22.
2. `RMS.SYS` mounts the FAT12 floppy, opens `RMSBDY.SYS` (LHA -lh5-), decompresses text/data to 0x1010000/0x1040500, checks CRC-16 and jumps to text+0x22 with SR 0x2700.
3. Kernel: memory init, ICUs/PITs/8251/SPF, slot scan, creates device tasks (keyboard, DMA, FDC, HDD, net), then `lodtsk` mounts the disk and loads `STRTUP.SYS` as the first MMU task.
4. STRTUP shows "Kernel ( SRMS V2.32 ) Started", reads `BOOT.MAP` and loads every `*.OBJ` from disks 1 and 2 ("COMPLETE"), restores `!BUPDB` from SRAM, then launches the application (`MAIN`).

## Open questions

* CPU-88 monitor EPROM contents (9100M0): bus-error recovery for board probing, debug TRAP #14 services, initial register contract.
* DSC-63 character generator ROM (CG banks 1-15): needed to render the large-font title screen.
* Exact PIT input clocks (8 MHz assumed from the 9600 baud divisor), IPL assignment, IDC board register maps, GPI pinout.
