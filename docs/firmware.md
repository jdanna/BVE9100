# Firmware inventory (`ref/`)

| Archive | Contents |
|---|---|
| `SS01WP_1/2` | BZE-9102 V3.04 system disks 1 and 2 (kernel, startup, application tasks) |
| `SS01WP_3/4` | same, second copy (2003) |
| `SS01BV_1/2` | BZE-9101 V3.02 system disks (bvem00-009) |
| `SS01CS_1` | BZE-9611 V5.03 mixer control: IDC firmware (`*.IDC`, `*.ID2`) and dialog modules (`*.HST`/`*.HS2`) for VSP-8000/DMX-E3000, GPS-600 (ESAM), GVG AMX-170, DESAM 800/400 |
| `SS01DC_1/2` | BVE-9000K maintenance program V1.32 (BKE-9000K1): `rms.sys` kernel, `strtup.sys`, `diag.sys`, `mainte.obj` |

## Files on a system disk (FAT12, 1.44 MB, label `BVE9100SYS1` / `BVE9100SYS2`)

| File | Role |
|---|---|
| `RMS.SYS` | boot loader, raw `.abs`, loads at 0x13E8000 |
| `RMSBDY.SYS` | SRMS kernel V03L20 (1992-09-03), LHA-compressed `.abs`, 1930 symbols |
| `STRTUP.SYS` | startup/loader task (LHA) |
| `RMSCFG.SYS`, `BOOT.MAP` | configuration text; BOOT.MAP lists the modules to load and `!BUPDB 804000 7c000` |
| `*.OBJ` | application tasks (LHA `.abs`), see `tools/absfile.py` |
| `IF508.FIL`, `IF508A/B.OBJ`, `HOSTS.FIL`, `SERVIC.FIL` | IF-508 Ethernet board firmware (MIPS ECOFF) and network config |
| `INS91A.REF` | installer menu definition |
| `VTRC24/25/30.IDT` | VTR constant tables (24/25/30 fps) |

## `.abs` load module format

See the docstring in `tools/absfile.py`. Key points: 0x1C-byte file header (magic 0x992D, symbol size,
text size, data size, text/data load addresses); text starts with a 0x24-byte module header (task id,
entry, data pointer, flags, 20-char version string); entry for kernel-type modules is text+0x22; symbol
records are 16 bytes (addr, type 0x0D text / 0x0E data, 11-char name). Extra task descriptors (14 bytes
each) sit at the end of `.data`.

## Task ids

`0x8221xxxx` application task (xxxx = task number), `0x8222xxxx` startup, `0x8121xxxx` diagnostics,
`0x00000000` kernel / shared library (`CMNDB` at 0xC0000000).
