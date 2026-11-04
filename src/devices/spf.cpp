#include "spf.h"
// GPI ports, as the editor drives them (GPI mode TEST FIRE and edit events): it loads the port's pulse timer (mode 4)
// with the pulse width (SETUP GPI-n PULSE WIDTH in ms x 16: the timers count at 16 kHz), for an edit as soon as the edit
// starts, and fires the port by writing its bit to the latch at 0xE03. Port 0 = latch bit 3 / 8254 #1 counter 0,
// ports 1-3 = latch bits 0-2 / 8254 #2 counters 0-2. Modelled as a flip-flop per port: the latch bit sets it, which
// raises the timer's GATE (so the timer counts the pulse width from the moment the port fires), and the timer's
// terminal-count strobe clears it. Only bits written as 1 act, as the editor writes one port's bit at a time.
Spf::Spf(bool pal) : pit1_("SPF.PIT1", 16000.0, 2), pit2_("SPF.PIT2", 16000.0, 2) {
    field_cycles_ = (double)CPU_HZ / (pal ? 50.0 : 59.94);
    for (int p = 0; p < 4; p++) gpi_timer(p).set_gate(gpi_ctr(p), false);
    pit1_.on_out = [this](int ch, bool lvl) { if (!lvl && ch == 0) gpi_end(0); };
    pit2_.on_out = [this](int ch, bool lvl) { if (!lvl) gpi_end(ch + 1); };
}
void Spf::gpi_end(int port) {
    if (!gpi_on_[port]) return;
    gpi_on_[port] = false;
    gpi_timer(port).set_gate(gpi_ctr(port), false);
    if (on_gpi) on_gpi(port, false);
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
    if (off == 0xE03 || off == 0xE07) { LOGD("SPF: GPI latch %03x = %02x\n", off, v); if (off == 0xE03) for (int b = 0; b < 4; b++) if (v >> b & 1) {
        int p = (b + 1) & 3;
        gpi_on_[p] = true; gpi_timer(p).set_gate(gpi_ctr(p), true);
        if (on_gpi) on_gpi(p, true);
    } }
    else LOGT("SPF: write %03x = %02x\n", off, v);
}
void Spf::tick(uint32_t cycles) {
    pit1_.tick(cycles); pit2_.tick(cycles);
    acc_ += cycles;
    // The interrupt (ICU1.3, vector 0x5B "fieldint") comes once per frame, at the start of field 1: the kernel's
    // field tick posts one event per interrupt and STRAPL advances its frame clocks by one frame per event.
    while (acc_ >= field_cycles_) { acc_ -= field_cycles_; field_++; if ((field_ & 1) == 0) { cf_ = (cf_ + 1) & 3; if (on_field) on_field(); } }
}
