#!/usr/bin/env python3
"""Sony SRMS ".abs" load-module parser (BVE-9100 / BVE-9000K firmware).

File layout (big-endian):
  0x00  u16  magic 0x992D
  0x02  u16  symbol table size in bytes (0 = stripped)
  0x04  u32  text size (includes the 0x24-byte module header that starts the text segment)
  0x08  u32  data size
  0x0C  u32  0
  0x10  u32  0
  0x14  u32  text load address
  0x18  u32  data load address
  0x1C       text segment (text size bytes), then data segment, then symbol table

Module header (first 0x24 bytes of text, also known in the kernel as `_hdrtib`):
  +0x00 u32 task id (0x8221xxxx = application task, 0x8222xxxx = startup, 0 = kernel/library)
  +0x04 u32 entry point (0 for kernel; kernel entry is text+0x22)
  +0x08 u32 initial data pointer / stack top
  +0x0C u16 flags
  +0x0E char[20] version string, e.g. "V1.00 910529KTASK   "
  (+0x22 word pad, +0x24 = first instruction for tasks)

Symbol table: 16-byte records {u32 addr, u8 type (0x0D = text, 0x0E = data), char[11] name}.
"""
import argparse, os, struct, subprocess, sys, tempfile

MAGIC = 0x992D

class Abs:
    def __init__(self, data: bytes, name: str = "?"):
        self.name = name
        if len(data) < 0x1C or struct.unpack('>H', data[:2])[0] != MAGIC:
            raise ValueError(f"{name}: not an SRMS .abs file")
        (self.magic, self.symsize, self.textsize, self.datasize, _, _,
         self.textaddr, self.dataaddr) = struct.unpack('>HHIIIIII', data[:0x1C])
        o = 0x1C
        self.text = data[o:o + self.textsize]; o += self.textsize
        self.data = data[o:o + self.datasize]; o += self.datasize
        self.symraw = data[o:o + self.symsize]
        self.task_id, self.entry, self.dataptr, self.flags = struct.unpack('>IIIH', self.text[:0x0E])
        self.version = self.text[0x0E:0x22].decode('latin1').rstrip('\0')
        self.symbols = []
        for i in range(0, len(self.symraw) - 15, 16):
            addr, typ = struct.unpack('>IB', self.symraw[i:i + 5])
            nm = self.symraw[i + 5:i + 16].split(b'\0')[0].decode('latin1')
            if addr == 0 and typ == 0:
                break
            self.symbols.append((addr, typ, nm))
        # Additional task descriptors live at the end of .data: {u32 id, u32 entry, u32 stack, u16 0} x N, then u32 0
        self.subtasks = []
        d = self.data
        i = len(d) - 4
        while i >= 14:
            tid, ent, stk, z = struct.unpack('>IIIH', d[i - 14:i])
            if (tid >> 16) in (0x8221, 0x8222, 0x8121) and z == 0:
                self.subtasks.insert(0, (tid, ent, stk)); i -= 14
            else:
                break

    @property
    def real_entry(self):
        return self.entry if self.entry else self.textaddr + 0x22

    def describe(self):
        print(f"{self.name}: '{self.version}' id={self.task_id:08x} entry={self.real_entry:08x} dataptr={self.dataptr:08x} flags={self.flags:04x}")
        print(f"  text {self.textaddr:08x}..{self.textaddr + self.textsize:08x} ({self.textsize} bytes)")
        print(f"  data {self.dataaddr:08x}..{self.dataaddr + self.datasize:08x} ({self.datasize} bytes)")
        print(f"  symbols: {len(self.symbols)}  subtasks: {[(hex(t), hex(e)) for t, e, s in self.subtasks]}")


def load(path: str, rawdir: str = None) -> Abs:
    """Load an .abs; transparently unpacks LHA (-lh5-) containers as used on the system disks."""
    raw = open(path, 'rb').read()
    if raw[2:7] == b'-lh5-' or raw[2:7] == b'-lh0-':
        with tempfile.TemporaryDirectory() as td:
            subprocess.run(['lha', 'xqw=' + td, os.path.abspath(path)], check=True, capture_output=True)
            found = [os.path.join(r, f) for r, _, fs in os.walk(td) for f in fs]
            raw = open(found[0], 'rb').read()
    if rawdir:
        os.makedirs(rawdir, exist_ok=True)
        out = os.path.join(rawdir, os.path.splitext(os.path.basename(path))[0].lower() + '.abs')
        open(out, 'wb').write(raw); print('raw ->', out)
    return Abs(raw, os.path.basename(path))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('files', nargs='+')
    ap.add_argument('--syms', action='store_true', help='dump symbol table')
    ap.add_argument('--extract', metavar='DIR', help='write <name>.text/.data/.sym into DIR')
    ap.add_argument('--raw', metavar='DIR', help='write the LHA-unpacked raw .abs file into DIR (for the emulator loader)')
    a = ap.parse_args()
    for f in a.files:
        m = load(f, a.raw)
        m.describe()
        if a.syms:
            for addr, typ, nm in m.symbols:
                print(f"  {addr:08x} {'T' if typ == 0x0D else 'D' if typ == 0x0E else hex(typ)} {nm}")
        if a.extract:
            os.makedirs(a.extract, exist_ok=True)
            base = os.path.join(a.extract, os.path.splitext(m.name)[0].lower())
            open(base + '.text', 'wb').write(m.text)
            open(base + '.data', 'wb').write(m.data)
            with open(base + '.sym', 'w') as fo:
                for addr, typ, nm in m.symbols:
                    fo.write(f"{addr:08x} {typ:02x} {nm}\n")

if __name__ == '__main__':
    main()
