import re, sys, collections
for f in sys.argv[1:]:
    cur='?'
    refs=collections.defaultdict(set)
    for line in open(f):
        if line.endswith(':\n') and not line.startswith(' '):
            cur=line.strip()[:-1]; continue
        for m in re.finditer(r'0x([0-9a-f]+)', line):
            v=int(m.group(1),16)
            # exclude RAM / module addresses
            if 0x800000<=v<0x900000 or 0x1000000<=v<0x1400000 or v<0x10000: continue
            if 0x80000000<=v<0x80100000 or 0x90000000<=v<0x90100000 or 0xC0000000<=v<0xC0100000 or 0xD0000000<=v<0xD0100000: continue
            refs[v].add(cur)
    print("#####", f)
    for v in sorted(refs):
        print(f"{v:08x}  {', '.join(sorted(refs[v]))[:200]}")
