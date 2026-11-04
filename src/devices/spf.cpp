#include "spf.h"
Spf::Spf(bool pal) : pit1_("SPF.PIT1", 1000000.0, 2), pit2_("SPF.PIT2", 1000000.0, 2) {
    field_cycles_ = (double)CPU_HZ / (pal ? 50.0 : 59.94);
    pit1_.on_out = [this](int ch, bool lvl) { if (on_gpi && !lvl) on_gpi(ch, false); };
}
uint8_t Spf::read8(uint32_t off) {
    off &= 0xFFF;
    if (off >= 0x000 && off < 0x008) return pit1_.read8(off);
    if (off >= 0x200 && off < 0x208) return pit2_.read8(off - 0x200);
    switch (off) {
    case 0xFD7: return board_id;
    // SPF-3/4 status (kernel theSpfIO[4]): bit6 = reference video present / field lock -> enables the
    // kernel's field tick (_tup2req); bit0 = field parity, bits1-2 = colour frame sequence (assumed).
    case 0xE09: return (uint8_t)(0x40 | (field_ & 1) | ((cf_ & 3) << 1));
    case 0xFD1: return (uint8_t)(0x80 | (field_ & 1) | ((cf_ & 7) << 1));   // ref present, field, colour frame
    case 0xFD3: return (uint8_t)(field_ & 0xFF);
    default: return latch_[off];
    }
}
void Spf::write8(uint32_t off, uint8_t v) {
    off &= 0xFFF;
    if (off >= 0x000 && off < 0x008) { pit1_.write8(off, v); return; }
    if (off >= 0x200 && off < 0x208) { pit2_.write8(off - 0x200, v); return; }
    latch_[off] = v;
    if (off == 0xE03 || off == 0xE07) { LOGD("SPF: GPI latch %03x = %02x\n", off, v); if (on_gpi) for (int i = 0; i < 4; i++) on_gpi(i, (v >> i) & 1); }
    else LOGT("SPF: write %03x = %02x\n", off, v);
}
void Spf::tick(uint32_t cycles) {
    pit1_.tick(cycles); pit2_.tick(cycles);
    acc_ += cycles;
    while (acc_ >= field_cycles_) { acc_ -= field_cycles_; field_++; if ((field_ & 1) == 0) cf_ = (cf_ + 1) & 3; if (on_field) on_field(); }
}
