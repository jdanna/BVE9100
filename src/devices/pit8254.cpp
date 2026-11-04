#include "pit8254.h"

void Pit8254::reset() { for (auto& c : c_) c = Counter(); acc_ = 0; }

uint8_t Pit8254::read8(uint32_t off) {
    int reg = (int)(off / stride_) & 3;
    if (reg == 3) return 0xFF;
    Counter& c = c_[reg];
    uint16_t v = c.latched ? c.latch : (uint16_t)c.count;
    uint8_t r;
    if (c.rw == 1) { r = (uint8_t)v; c.latched = false; }
    else if (c.rw == 2) { r = (uint8_t)(v >> 8); c.latched = false; }
    else {
        if (c.rd_phase == 0) { r = (uint8_t)v; c.rd_phase = 1; }
        else { r = (uint8_t)(v >> 8); c.rd_phase = 0; c.latched = false; }
    }
    return r;
}

void Pit8254::write8(uint32_t off, uint8_t v) {
    int reg = (int)(off / stride_) & 3;
    if (reg == 3) {
        int sc = v >> 6;
        if (sc == 3) return;            // read-back command (8254): not needed yet
        Counter& c = c_[sc];
        int rw = (v >> 4) & 3;
        if (rw == 0) { c.latch = (uint16_t)c.count; c.latched = true; return; }
        c.rw = rw; c.mode = (v >> 1) & 7; if (c.mode > 5) c.mode -= 4; c.bcd = v & 1;
        c.wr_phase = 0; c.rd_phase = 0; c.loaded = false; c.armed = false; c.null_count = true;
        c.out = (c.mode == 0) ? false : true;
        LOGD("%s: ctr%d mode %d rw %d\n", nm_, sc, c.mode, c.rw);
        return;
    }
    Counter& c = c_[reg];
    if (c.rw == 1) { c.reload = v; load(reg); }
    else if (c.rw == 2) { c.reload = (uint16_t)(v << 8); load(reg); }
    else {
        if (c.wr_phase == 0) { c.reload = (c.reload & 0xFF00) | v; c.wr_phase = 1; }
        else { c.reload = (uint16_t)((c.reload & 0x00FF) | (v << 8)); c.wr_phase = 0; load(reg); }
    }
}

void Pit8254::load(int ch) {
    Counter& c = c_[ch];
    c.count = c.reload ? c.reload : 0x10000;
    c.loaded = true; c.armed = true; c.null_count = false;
    LOGD("%s: ctr%d load %u (%.1f Hz)\n", nm_, ch, c.count, clk_ / (double)c.count);
    if (c.mode == 0) c.out = false;
}

void Pit8254::clock(int ch, int n) {
    Counter& c = c_[ch];
    if (!c.armed) return;
    while (n > 0) {
        int step = n < c.count ? n : c.count;
        if (step <= 0) step = 1;
        c.count -= step; n -= step;
        if (c.count <= 0) {
            switch (c.mode) {
            case 0: c.out = true; c.armed = false; c.count = 0; if (on_out) on_out(ch, true); break;
            case 1: c.out = true; c.armed = false; c.count = 0; if (on_out) on_out(ch, true); break;
            case 2: // rate generator: pulse low for one clock then reload
                c.count = c.reload ? c.reload : 0x10000;
                if (on_out) { on_out(ch, false); on_out(ch, true); }
                break;
            case 3: // square wave: toggle every half period
                c.out = !c.out; c.count = (c.reload ? c.reload : 0x10000) / 2; if (c.count == 0) c.count = 1;
                if (on_out) on_out(ch, c.out);
                break;
            case 4: case 5: // strobe
                c.out = false; if (on_out) { on_out(ch, false); on_out(ch, true); } c.out = true; c.armed = false; c.count = 0;
                break;
            }
        }
    }
}

void Pit8254::tick(uint32_t cycles) {
    acc_ += (double)cycles * clk_ / (double)CPU_HZ;
    int n = (int)acc_;
    if (n <= 0) return;
    acc_ -= n;
    for (int i = 0; i < 3; i++) clock(i, n);
}
