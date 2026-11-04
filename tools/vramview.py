#!/usr/bin/env python3
"""Render a DSC-63 VRAM dump (from --vram-dump) as text, marking non-ASCII cells.

usage: vramview.py DUMP [--codes]
  Cells: bank 0 printable = the character; anything else = a two-hex-digit code is listed per row
  with --codes as  bank:char@column.
"""
import struct, sys
from collections import Counter

def load(p):
    d = open(p, 'rb').read()
    return d[:0x10000], d[0x10000:0x1C000], d[0x1C000:0x1C800]

def main():
    vram, p2, regs = load(sys.argv[1])
    codes = '--codes' in sys.argv
    start = ((regs[0x180] << 8) | regs[0x181]) << 2
    parts = [(((regs[0x180 + 2*i] << 8) | regs[0x181 + 2*i]) << 2, (regs[0x190 + 2*i] << 8) | regs[0x191 + 2*i]) for i in range(4)]
    print('partitions (vram addr, rows):', [(hex(a), n) for a, n in parts])
    census = Counter()
    row = 0
    for pa, n in parts:
        for r in range(n):
            base = pa + r * 0x140
            line, extra = '', []
            for c in range(80):
                a = (base + c * 4) & 0xFFFF
                cell = vram[a:a+4]
                if a + 4 <= len(p2) and p2[a+1] != 0x20 and (p2[a] | p2[a+1]):
                    cell = p2[a:a+4]
                bank, ch = cell[0] & 0x0F, cell[1]
                attr = (cell[2] << 8) | cell[3]
                if bank == 0 and 0x20 <= ch < 0x7F:
                    line += chr(ch)
                elif bank == 0 and ch == 0:
                    line += ' '
                else:
                    line += '#' if bank else '~'
                    census[(bank, ch, attr & 0x800F)] += 1
                    extra.append('%x:%02x/%x@%d' % (bank, ch, attr, c))
            print('%2d|%s' % (row, line.rstrip()))
            if codes and extra:
                print('   ', ' '.join(extra[:40]) + (' ...' if len(extra) > 40 else ''))
            row += 1
    print('census (bank, char, colour|reverse): count')
    for k, v in sorted(census.items()):
        print('  bank %x char %02x attr %04x : %d' % (k[0], k[1], k[2], v))

if __name__ == '__main__':
    main()
