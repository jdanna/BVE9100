#include "bus.h"
#include <set>

int g_verbose = 1;
uint64_t g_cycles = 0;

void Bus::unmapped(uint32_t a, bool w) {
    static std::set<uint32_t> seen;
    if (seen.insert(a & ~0xFFu).second)
        LOGI("BUS: unmapped %s at %08x (further hits in this 256-byte page not logged)\n", w ? "write" : "read", a);
    if (on_unmapped) on_unmapped(a, w);
}

uint8_t Bus::read8(uint32_t a) {
    uint32_t off; Device* d = find(a, &off);
    if (!d) { unmapped(a, false); return 0xFF; }
    return d->read8(off);
}
uint16_t Bus::read16(uint32_t a) {
    uint32_t off; Device* d = find(a, &off);
    if (!d) { unmapped(a, false); return 0xFFFF; }
    uint32_t off2; Device* d2 = find(a + 1, &off2);
    if (d2 != d) return (uint16_t)((d->read8(off) << 8) | read8(a + 1));
    return d->read16(off);
}
uint32_t Bus::read32(uint32_t a) {
    uint32_t off; Device* d = find(a, &off);
    if (!d) { unmapped(a, false); return 0xFFFFFFFF; }
    uint32_t off2; Device* d2 = find(a + 3, &off2);
    if (d2 != d) return ((uint32_t)read16(a) << 16) | read16(a + 2);
    return d->read32(off);
}
void Bus::write8(uint32_t a, uint8_t v) {
    uint32_t off; Device* d = find(a, &off);
    if (!d) { unmapped(a, true); return; }
    d->write8(off, v);
}
void Bus::write16(uint32_t a, uint16_t v) {
    uint32_t off; Device* d = find(a, &off);
    if (!d) { unmapped(a, true); return; }
    uint32_t off2; Device* d2 = find(a + 1, &off2);
    if (d2 != d) { d->write8(off, (uint8_t)(v >> 8)); write8(a + 1, (uint8_t)v); return; }
    d->write16(off, v);
}
void Bus::write32(uint32_t a, uint32_t v) {
    uint32_t off; Device* d = find(a, &off);
    if (!d) { unmapped(a, true); return; }
    uint32_t off2; Device* d2 = find(a + 3, &off2);
    if (d2 != d) { write16(a, (uint16_t)(v >> 16)); write16(a + 2, (uint16_t)v); return; }
    d->write32(off, v);
}
