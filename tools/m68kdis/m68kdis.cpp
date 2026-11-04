// m68kdis: disassemble a flat binary image with Musashi.
// usage: m68kdis FILE LOADADDR START END [cpu]   (addresses hex; cpu = 68000 | 68030, default 68030)
// Used for code Ghidra did not analyse (kernel routines in the data segment, IDC sub-CPU firmware).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
extern "C" {
#include "m68k.h"
}
static std::vector<unsigned char> img;
static unsigned base;
static unsigned rd(unsigned a, int n) {
    unsigned v = 0;
    for (int i = 0; i < n; i++) { unsigned o = a + i - base; v = (v << 8) | (o < img.size() ? img[o] : 0); }
    return v;
}
extern "C" {
unsigned int m68k_read_disassembler_8(unsigned int a) { return rd(a, 1); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return rd(a, 2); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return rd(a, 4); }
}
int main(int argc, char** argv) {
    if (argc < 5) { fprintf(stderr, "usage: %s FILE LOADADDR START END [68000|68030]\n", argv[0]); return 2; }
    FILE* f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 1; }
    int c; while ((c = fgetc(f)) != EOF) img.push_back((unsigned char)c); fclose(f);
    base = strtoul(argv[2], nullptr, 16);
    unsigned pc = strtoul(argv[3], nullptr, 16), end = strtoul(argv[4], nullptr, 16);
    unsigned type = (argc > 5 && !strcmp(argv[5], "68000")) ? M68K_CPU_TYPE_68000 : M68K_CPU_TYPE_68030;
    char buf[256];
    while (pc < end) {
        unsigned n = m68k_disassemble(buf, pc, type);
        if (!n) n = 2;
        printf("%08x  ", pc);
        for (unsigned i = 0; i < 10; i += 2) { if (i < n) printf("%04x ", rd(pc + i, 2)); else printf("     "); }
        printf(" %s\n", buf);
        pc += n;
    }
    return 0;
}
