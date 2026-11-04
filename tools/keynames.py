#!/usr/bin/env python3
"""Dump the BVE-9100 key-function name table from CMNDB.

CMNDB data holds:
  0xD0099544  332 x 12-byte names (11 chars + NUL), alphabetical
  0xD009A508  64x6 pointers to those names (KEY ASSIGN menu grid)
  0xD0097644  64x6 u16 function codes, parallel to the pointer grid
  0xD0096A44  factory key-assignment table: 768 u16, index = wire code
              (plane 0 = plain byte, 1 = prefix 0x80, 2 = prefix 0x81),
              value = function code dispatched by MAIN.

Usage: python3 tools/keynames.py [work/raw/cmndb.abs]
"""
import struct
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import absfile  # noqa: E402

NAMES = 0xD0099544
CODES = 0xD0097644
FACTORY = 0xD0096A44
N = 332


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else 'work/raw/cmndb.abs'
    m = absfile.load(path)
    d, b = m.data, m.dataaddr
    names = [d[NAMES - b + i * 12:NAMES - b + i * 12 + 11].decode().rstrip()
             for i in range(N)]
    codes = struct.unpack('>%dH' % N, d[CODES - b:CODES - b + 2 * N])
    tab = struct.unpack('>768H', d[FACTORY - b:FACTORY - b + 0x600])
    inv = {}
    for w, f in enumerate(tab):
        if f:
            inv.setdefault(f, []).append(w)

    def wire(w):
        p, c = w >> 8, w & 0xFF
        return ('', '80 ', '81 ')[p] + '%02x' % c

    print('func  name          wire codes (factory)')
    for c, n in sorted(zip(codes, names)):
        ws = ', '.join(wire(w) for w in inv.get(c, []))
        print('%03x   %-12s  %s' % (c, n, ws))


if __name__ == '__main__':
    main()
