#!/usr/bin/env python3
"""Build a 1.44 MB (or 720 KB) FAT12 floppy image from a directory of files.

The BVE-9100 system disks are plain MS-DOS FAT12 floppies (the RMS.SYS boot loader's
filesystem code checks media bytes 0xF0/0xF9 and reads a standard BPB). The files on the
installer floppies (*.OBJ, RMSBDY.SYS, STRTUP.SYS) are LHA (-lh5-) containers that the boot
loader and kernel decompress themselves, so they are copied verbatim.

usage: mkfat12.py OUT.img SRC_DIR [--label BVE9100SYS1] [--dd]
"""
import argparse, os, struct, time

def build(out, srcdir, label, dd=False):
    bps = 512
    if dd:
        spt, heads, tracks, spf, media, root_ents = 9, 2, 80, 3, 0xF9, 112
    else:
        spt, heads, tracks, spf, media, root_ents = 18, 2, 80, 9, 0xF0, 224
    total = spt * heads * tracks
    spc = 1
    reserved, nfats = 1, 2
    root_secs = root_ents * 32 // bps
    data_start = reserved + nfats * spf + root_secs
    nclusters = total - data_start
    img = bytearray(total * bps)
    label = (label or 'NO NAME').upper().ljust(11)[:11].encode('ascii')
    # --- boot sector / BPB ---
    bs = bytearray(bps)
    bs[0:3] = b'\xEB\x3C\x90'; bs[3:11] = b'BVE9100 '
    struct.pack_into('<HBHBHHBHHHII', bs, 11, bps, spc, reserved, nfats, root_ents, total, media, spf, spt, heads, 0, 0)
    bs[36] = 0; bs[38] = 0x29; struct.pack_into('<I', bs, 39, int(time.time()) & 0xFFFFFFFF)
    bs[43:54] = label; bs[54:62] = b'FAT12   '
    bs[510] = 0x55; bs[511] = 0xAA
    img[0:bps] = bs
    # --- FAT ---
    fat = bytearray(spf * bps)
    def set_fat(n, val):
        off = n * 3 // 2
        if n & 1:
            fat[off] = (fat[off] & 0x0F) | ((val << 4) & 0xF0); fat[off + 1] = (val >> 4) & 0xFF
        else:
            fat[off] = val & 0xFF; fat[off + 1] = (fat[off + 1] & 0xF0) | ((val >> 8) & 0x0F)
    set_fat(0, 0xF00 | media); set_fat(1, 0xFFF)
    # --- root directory ---
    root = bytearray(root_secs * bps)
    ent = 0
    def dosname(fn):
        base, ext = os.path.splitext(fn.upper())
        return base[:8].ljust(8) + ext[1:4].ljust(3)
    def put_entry(name11, attr, cluster, size, dt):
        nonlocal ent
        e = bytearray(32)
        e[0:11] = name11.encode('ascii'); e[11] = attr
        t = time.localtime(dt)
        dos_time = (t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec // 2)
        dos_date = ((max(t.tm_year, 1980) - 1980) << 9) | (t.tm_mon << 5) | t.tm_mday
        struct.pack_into('<HH', e, 22, dos_time, dos_date)
        struct.pack_into('<HI', e, 26, cluster, size)
        root[ent * 32:(ent + 1) * 32] = e; ent += 1
    put_entry(label.decode('ascii'), 0x08, 0, 0, time.time())
    next_cluster = 2
    files = sorted(f for f in os.listdir(srcdir) if os.path.isfile(os.path.join(srcdir, f)) and not f.startswith('.'))
    for fn in files:
        data = open(os.path.join(srcdir, fn), 'rb').read()
        n = (len(data) + bps * spc - 1) // (bps * spc)
        if next_cluster + n - 2 > nclusters: raise SystemExit(f"disk full at {fn}")
        first = next_cluster if n else 0
        for i in range(n):
            c = next_cluster + i
            set_fat(c, 0xFFF if i == n - 1 else c + 1)
            sec = data_start + (c - 2) * spc
            chunk = data[i * bps * spc:(i + 1) * bps * spc]
            img[sec * bps:sec * bps + len(chunk)] = chunk
        next_cluster += n
        put_entry(dosname(fn), 0x20, first, len(data), os.path.getmtime(os.path.join(srcdir, fn)))
        print(f"  {dosname(fn)} {len(data):8d} bytes, {n} clusters")
    for i in range(nfats):
        img[(reserved + i * spf) * bps:(reserved + (i + 1) * spf) * bps] = fat
    img[(reserved + nfats * spf) * bps:(reserved + nfats * spf) * bps + len(root)] = root
    open(out, 'wb').write(img)
    print(f"wrote {out}: {len(files)} files, {next_cluster - 2}/{nclusters} clusters used, label '{label.decode()}'")

if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out'); ap.add_argument('srcdir'); ap.add_argument('--label', default='BVE9100SYS1'); ap.add_argument('--dd', action='store_true')
    a = ap.parse_args()
    build(a.out, a.srcdir, a.label, a.dd)
