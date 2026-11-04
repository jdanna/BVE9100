# BKE-9400A keyboard wire codes

Each key press is one wire code from the keyboard: a plain byte, or a prefix byte `80` / `81` followed by a byte.
**SHIFT and CTRL are resolved inside the keyboard** - a key with a top legend (SHIFT) or bottom legend (CTRL) sends
a *different* code for each combination; nothing is prefixed by the host. The kernel's keyboard driver turns
`80 xx` / `81 xx` into 16-bit codes 0x1xx / 0x2xx, KTASK translates the 16-bit wire code through the 768-entry
key-assignment table (factory copy at CMNDB 0xD0096A44, live copy behind the pointer at 0xD0096434) into a
*function code*, and MAIN's dispatch table (6-byte records {handler, mode mask} at MAIN data 0x9000B1A2) runs the
handler for that function code.

The function names below come from the KEY ASSIGN menu's name table in CMNDB (332 names at 0xD0099544, parallel
code array at 0xD0097644; `tools/keynames.py` regenerates this list). "wire" is the factory assignment; the user
can reassign keys from INIT MENU, which only changes the live table.

Legend convention on the key caps: main legend = plain press, top legend = SHIFT, bottom (small) legend = CTRL.
Examples: key `3` sends `33` (3), SHIFT sends `23` (MOD EDL), CTRL sends `93` (STOR ED). Letter keys send the
lower-case ASCII code for the main legend (a = P1 = `61`), the upper-case code for the top legend (A3 on `I` = `49`),
and ASCII control codes for most CTRL legends (CTRL+E = REC = `05`). Verified in the emulator: `93` on the edit page
answers "DEFINE INPOINT R P1"; `e0` opens INIT MENU 1; `80 22` / `80 21` answer YES / NO prompts.

Functions with no wire code are assignable from the KEY ASSIGN menu but have no key in the factory layout.

| func | name | wire (factory) |
|---|---|---|
| 001 | P7 | 01, 81 11 |
| 002 | EJECT | 02 |
| 003 | STANDBY OFF | 03 |
| 004 | P9 | 04, 81 13 |
| 005 | REC | 05 |
| 006 | P10 | 06, 81 14 |
| 007 | P11 | 07, 81 15 |
| 008 | P12 | 08, 81 16 |
| 009 | PREREAD | 09 |
| 00a | ENTER | 0a |
| 00b | SY-TIME | 0b |
| 00d | RET | 0d |
| 00e | TRACK SCROL | 0e |
| 010 | CTL+CUT | 10 |
| 011 | P-LEARN | 11 |
| 013 | P8 | 13, 81 12 |
| 014 | PREROLL | 14 |
| 015 | 1ST - ED | 15 |
| 016 | MEM PLAY | 16 |
| 017 | S QUICK PVW | 17 |
| 018 | FREEZE-ON | 18 |
| 019 | BAK AUX | 19 |
| 01a | FREEZE-OFF | 1a |
| 01c | BANK FS | 1c |
| 01e | BANK BS | 1e |
| 01f | MAINTENANCE | 1f |
| 020 | ALL STOP | 20, 81 97 |
| 021 | LOAD EDL | 21 |
| 022 | SAVE EDL | 22 |
| 023 | MOD EDL | 23 |
| 024 | CRCT | 24 |
| 025 | INSRT | 25 |
| 026 | DEL | 26 |
| 027 | MOVE | 27 |
| 028 | NOTE | 28 |
| 029 | LAST EDIT | 29 |
| 02a | PTNKY | 2a |
| 02b | + | 2b, 80 2b |
| 02c | MARK-OUT | 2c, 81 86 |
| 02d | - | 2d, 80 2d |
| 02e | MARK-CONST | 2e, 81 93 |
| 02f | RECAL-CONST | 2f |
| 030 | 0 | 30, 80 30 |
| 031 | 1 | 31, 80 31 |
| 032 | 2 | 32, 80 32 |
| 033 | 3 | 33, 80 33 |
| 034 | 4 | 34, 80 34 |
| 035 | 5 | 35, 80 35 |
| 036 | 6 | 36, 80 36 |
| 037 | 7 | 37, 80 37 |
| 038 | 8 | 38, 80 38 |
| 039 | 9 | 39, 80 39 |
| 03a | KEY | 3a |
| 03b | SET DUR | 3b |
| 03c | MARK-DLY | 3c |
| 03d | BACK-DUR | 3d |
| 03e | MARK-USR | 3e |
| 03f | STORE CONST | 3f |
| 043 | STOP | 43, 81 a2 |
| 045 | AUTO REC | 45 |
| 049 | A3 | 49 |
| 04a | SPEED | 4a |
| 04b | BACK-IN | 4b |
| 04c | BACK-OUT | 4c |
| 04d | MARK-SPLIT | 4d |
| 04e | ACT TRACK | 4e |
| 04f | A4 | 4f |
| 051 | MPPVW | 51 |
| 052 | H CACHE PVW | 52 |
| 054 | SYNC PLAY | 54 |
| 055 | ASMBL | 55 |
| 056 | SCAN | 56, 81 a3 |
| 057 | SWPVW | 57 |
| 058 | PLAY + | 58 |
| 059 | AUX | 59 |
| 05a | PLAY - | 5a |
| 05b | WIPE | 5b |
| 05c | FS | 5c |
| 05d | MAN | 5d |
| 05e | BS | 5e |
| 060 | DISS | 60 |
| 061 | P1 | 61, d1, 81 89 |
| 062 | SLOW | 62, 81 84 |
| 063 | STILL | 63, 81 82 |
| 064 | P3 | 64, d3, 81 8b |
| 065 | REC OFF | 65 |
| 066 | P4 | 66, d4, 81 8c |
| 067 | P5 | 67, d5, 81 8d |
| 068 | P6 | 68, d6, 81 8e |
| 069 | A1 | 69 |
| 06a | SPLIT | 6a |
| 06b | SET IN | 6b, 81 90 |
| 06c | SET OUT | 6c, 81 91 |
| 06d | MARK-IN | 6d, 81 85 |
| 06e | TIME TRACK | 6e |
| 06f | A2 | 6f |
| 070 | CUT | 70 |
| 071 | P-PVW | 71 |
| 072 | REPLAY | 72 |
| 073 | P2 | 73, d2, 81 8a |
| 074 | GO TO | 74 |
| 075 | V | 75 |
| 076 | PLAY | 76, 81 83 |
| 077 | R-PVW | 77 |
| 078 | FF | 78, 81 8f |
| 079 | GPI | 79 |
| 07a | REW | 7a, 81 87 |
| 07c | XCHG | 7c |
| 07d | MAN-R | 7d |
| 07e | SAVE | 7e |
| 07f | DISK | 7f |
| 090 | RECAL SCROL | 90 |
| 093 | STOR ED | 93 |
| 094 | APPEND | 94 |
| 097 | COPY | 97 |
| 09b | BACK SCROL | 9b |
| 09c | LAST MARK | 9c |
| 09e | MULTI-SCRPD | 9e |
| 09f | RECAL SEGMT | 9f |
| 0a1 | LEARN | a1 |
| 0a2 | ALL R | a2, 81 10 |
| 0a4 | RECALL | a4 |
| 0a5 | CTL+DISS | a5 |
| 0a7 | MRK SPD | a7 |
| 0a8 | CTL+ALLSTOP | a8 |
| 0a9 | CTL+RETURN | a9 |
| 0aa | CTL+ENTER | aa |
| 0ae | MEM JOG | ae |
| 0b0 | MONI-ALL R | b0 |
| 0b1 | MONI-P7 | b1 |
| 0b2 | MONI-P8 | b2 |
| 0b3 | MONI-P9 | b3 |
| 0b4 | MONI-P10 | b4 |
| 0b5 | MONI-P11 | b5 |
| 0b6 | MONI-P12 | b6 |
| 0b7 | #1 | b7 |
| 0b8 | #2 | b8 |
| 0b9 | EFF (XPT) | b9 |
| 0ba | SUPER | ba |
| 0c1 | MULTI-PVW | c1 |
| 0c8 | SFT+ALLSTOP | c8 |
| 0c9 | SFT+RETURN | c9 |
| 0ca | SFT+ENTER | ca |
| 0ce | SYNC JOG | ce |
| 0d7 | AUX1 | d7 |
| 0d8 | AUX2 | d8 |
| 0d9 | BLACK | d9 |
| 0e0 | INIT | e0 |
| 0e1 | PREVIEW | e1, 81 9f |
| 0e2 | R | d0, e2, 81 88 |
| 0e3 | STOP/START | e3 |
| 0ed | SHTL | ed |
| 0ee | JOG | ee |
| 0ef | DMC | ef |
| 0f0 | MONI-R | f0 |
| 0f1 | MONI-P1 | f1 |
| 0f2 | MONI-P2 | f2 |
| 0f3 | MONI-P3 | f3 |
| 0f4 | MONI-P4 | f4 |
| 0f5 | MONI-P5 | f5 |
| 0f6 | MONI-P6 | f6 |
| 0f7 | MONI-AUX1 | f7, 81 94 |
| 0f8 | MONI-AUX2 | f8, 81 95 |
| 0f9 | MONI-BLACK | f9, 81 96 |
| 0fa | MONI-A1 | fa |
| 0fb | MONI-A2 | fb |
| 0fc | MONI-A3 | fc |
| 0fd | MONI-A4 | fd |
| 101 | EDIT | 80 01 |
| 102 | MA/SL | 80 02 |
| 103 | REEL | 80 03 |
| 104 | TMP_R | 80 04 |
| 105 | SY-GR | 80 05 |
| 106 | SCRPD | 80 06 |
| 107 | DEL EVENT | 80 07 |
| 108 | MOD EVENT | 80 08 |
| 109 | CREAT EVENT | 80 09 |
| 10b | SCROL | 80 0b |
| 10c | GAP | 80 0c |
| 10e | SAVE SSU | 80 0e |
| 10f | MULTI-EDL | 80 0f |
| 110 | QUICK-TRACE | 80 10 |
| 111 | LOAD TAPE | 80 11 |
| 112 | SWER LOCAL | 80 12 |
| 113 | PF1 | 80 13 |
| 114 | PF2 | 80 14 |
| 115 | PF3 | 80 15 |
| 116 | PF4 | 80 16 |
| 118 | F1 | 80 18 |
| 119 | F2 | 80 19 |
| 11a | F3 | 80 1a |
| 11b | F4 | 80 1b |
| 11c | F5 | 80 1c |
| 11d | F6 | 80 1d |
| 11e | F7 | 80 1e |
| 11f | F8 | 80 1f |
| 121 | NO | 80 21 |
| 122 | YES | 80 22 |
| 123 | CLR 1 | 80 23 |
| 124 | CURSOR UP | 80 24 |
| 125 | CURSOR LEFT | 80 25 |
| 126 | CURSOR DOWN | 80 26 |
| 127 | CURSORRIGHT | 80 27 |
| 12a | * | 80 2a |
| 12f | / | 80 2f |
| 13a | 00 | 80 3a |
| 13b | F/TC | 80 3b |
| 13d | = | 80 3d |
| 141 | BLOCK | 80 41 |
| 143 | XPT-ASSIGN | 80 43 |
| 144 | U-BIT | 80 44 |
| 145 | DMC RANGE | 80 45 |
| 146 | OUT | 80 46 |
| 147 | MXMEM | 80 47 |
| 148 | SWMEM | 80 48 |
| 149 | DMC MEM | 80 49 |
| 14a | RENUM | 80 4a |
| 14b | SORT | 80 4b |
| 14c | CLEAN | 80 4c |
| 14d | TEST | 80 4d |
| 14e | LOAD SSU | 80 4e |
| 14f | SET UP | 80 4f |
| 150 | TRACE | 80 50 |
| 151 | UNLOAD TAPE | 80 51 |
| 152 | MIXER LOCAL | 80 52 |
| 153 | PF5 | 80 53 |
| 154 | PF6 | 80 54 |
| 155 | PF7 | 80 55 |
| 156 | PF8 | 80 56 |
| 163 | CLEAR | 80 63 |
| 164 | SFT+UP | 80 64 |
| 165 | SFT+LEFT | 80 65 |
| 166 | SFT+DOWN | 80 66 |
| 167 | SFT+RIGHT | 80 67 |
| 16d | - (SFT + -) | 80 6d |
| 16f | LAST-X | 80 6f |
| 170 | SFT+0 | 80 70 |
| 171 | SFT+1 | 80 71 |
| 172 | SFT+2 | 80 72 |
| 173 | SFT+3 | 80 73 |
| 174 | SFT+4 | 80 74 |
| 175 | SFT+5 | 80 75 |
| 176 | SFT+6 | 80 76 |
| 177 | SFT+7 | 80 77 |
| 178 | SFT+8 | 80 78 |
| 179 | SFT+9 | 80 79 |
| 17d | ARITH | 80 7d |
| 183 | AUD ASIGN | 80 83 |
| 184 | ARCHIVE | 80 84 |
| 185 | PGM PLAY | 80 85 |
| 186 | EFF (PVW) | 80 86 |
| 188 | SEG PLY MEM | - |
| 189 | CC MEM | 80 89 |
| 18b | AUTO SCROL | 80 8b |
| 18e | LOAD PPF | 80 8e |
| 18f | TMR-RESET | 80 8f |
| 191 | CACHE MNG | 80 91 |
| 192 | DME LOCAL | 80 92 |
| 193 | LRN PF | 80 93 |
| 196 | DISP PF | 80 96 |
| 198 | CTL+F1 | 80 98 |
| 199 | CTL+F2 | 80 99 |
| 19a | CTL+F3 | 80 9a |
| 19b | CTL+F4 | 80 9b |
| 19c | CTL+F5 | 80 9c |
| 19d | CTL+F6 | 80 9d |
| 19e | CTL+F7 | 80 9e |
| 19f | CTL+F8 | 80 9f |
| 1a3 | CLR ALL | 80 a3 |
| 1a4 | CTL+UP | 80 a4 |
| 1a5 | CTL+LEFT | 80 a5 |
| 1a6 | CTL+DOWN | 80 a6 |
| 1a7 | CTL+RIGHT | 80 a7 |
| 1af | CTL - / | 80 af |
| 1d3 | PF9 | 80 d3, 81 98 |
| 1d4 | PF10 | 80 d4, 81 99 |
| 1d5 | PF11 | 80 d5, 81 9a |
| 1d6 | PF12 | 80 d6, 81 9b |
| 1df | PF LPMODE | 80 df |
| 1e4 | CTL+SFT+UP | 80 e4 |
| 1e6 | CTL+SFT+DWN | 80 e6 |
| 20b | CLP SL KEY1 | - |
| 20c | CLP SL KEY2 | - |
| 20e | RECALL BTM | - |
| 20f | RECALL TOP | - |
| 229 | CLP SC UP | - |
| 22a | CLP SC DOWN | - |
| 22b | CLP SC RIGT | - |
| 22c | CLP SC LEFT | - |
| 22e | TIME LINE | - |
| 22f | KF MEM | - |
| 23c | FIELD EDIT | - |
| 23d | MC-PREVIEW | - |
| 23e | EXIT INDY | - |
| 23f | REEL SUMARY | - |
| 244 | CNCT INDY | - |
| 245 | DSCNCT INDY | - |
| 246 | UNDO EDL | - |
| 247 | BACKUP | - |
| 24a | CLP UDM A | - |
| 24b | CLP UDM EFF | - |
| 24c | CLP UDM B | - |
| 250 | PF13 | 81 9c |
| 251 | PF14 | 81 9d |
| 252 | PF15 | 81 9e |
| 253 | PF16 | - |
| 254 | AUTO TRACK | - |
| 255 | R TRACK | - |
| 256 | TRACK MENU | - |
| 257 | STOPCODE | - |
| 258 | OPEN END | - |
| 259 | MARK-SPLOUT | - |
| 25a | JOG FWD | - |
| 25f | JOG REV | - |
| 262 | AUTO CACHE | - |
| 263 | RECACHE | - |
| 2b8 | R1 | 81 b8 |
| 2b9 | R2 | 81 b9 |
| 2ba | R3 | 81 ba |
| 2bb | R4 | 81 bb |
| 2bc | R5 | 81 bc |
| 2bd | R6 | 81 bd |
| 2be | R7 | 81 be |
| 2bf | R8 | 81 bf |
| 2d8 | MONI-R1 | 81 d8 |
| 2d9 | MONI-R2 | 81 d9 |
| 2da | MONI-R3 | 81 da |
| 2db | MONI-R4 | 81 db |
| 2dc | MONI-R5 | 81 dc |
| 2dd | MONI-R6 | 81 dd |
| 2de | MONI-R7 | 81 de |
| 2df | MONI-R8 | 81 df |
