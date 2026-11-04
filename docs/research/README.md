# Research notes (web, 2026-11)

Key sources (full agent report summarised):

* Sony brochure BC-00346-A: https://www.thameside.tv/documents/BVE-9100.pdf (saved here). 32-bit CPU @ 20 MHz,
  option table (BKE-900x/901x/94xx/95xx/96xx), rear panel: REF VIDEO IN/OUT, MAIN KEYBOARD, PRINTER, AUX, GPI,
  FIELD REF IN, B/W VDU, colour VDU 80x29 16 colours.
* BZE-9101/9102 User's Guide Vol.1 (German, 1992): https://archive.org/details/manual_BVE9100_OM_SONY_DE
  (OCR text saved here). Setup menus, drive letters (A/B MFD, E/F 8", G HDD, I/J BKE-9500, M/N 8"), boot
  sequence with system disks 1-3, self-diagnosis board list CPU-88 / DSC-63 / SIO-3/4 / SPF-3/4/4A.
* Sony release notes BZE-9101 V3.02: https://pro.sony/s3/cms-static-content/software/PRE-01BV.PDF
  (ROM "9100M0 V1.11" on CPU-88, BKE-9107 HDD Seagate ST34520N, volume names BVE9100SYS1/2).
* Mixer software BZE-9611 install manual: https://pro.sony/s3/cms-static-content/software/S-BVE-003.PDF
* BKE-9400A keyboard I&M manual (86 pp): https://www.manualslib.com/manual/2942266/Sony-Bke-9400a.html
* Board/BKE mapping from listings: SIO-3 = BKE-9002 (1-620-367-12), IF-263 = BKE-9004A (1-631-528-11),
  IF-328 = BKE-9012 (1-637-805-11), IF-328B = BKE-9014.
* Spec snippets (bcs.tv / broadcaststore): 4 MB DRAM, 512 KB SRAM, 6000-edit EDL.
* Protocols: Sony 9-pin (38.4 kbaud 8-O-1), ESAM-II (DHD PDF saved here, Editware notes saved here),
  Avid EDL Manager formats "Sony_9100 1.02_1.04 / 1.1_2.11" (PDF saved here).
* Nothing public exists on the IF-508 Ethernet/Indy option, the SRMS OS, or any prior emulation.
