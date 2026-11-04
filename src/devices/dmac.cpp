#include "dmac.h"
void Dmac::reset() { for (auto& c : c_) c = Ch(); sel_ = 0; cmd_ = 0; mask_ = 0xF; req_ = 0; status_ = 0; }
uint8_t Dmac::read8(uint32_t off) {
    switch (off & 0xF) {
    case 0x0: return (uint8_t)sel_;
    case 0x2: return (uint8_t)(c_[sel_].count >> 8);
    case 0x3: return (uint8_t)c_[sel_].count;
    case 0x4: return (uint8_t)(c_[sel_].addr >> 8);
    case 0x5: return (uint8_t)c_[sel_].addr;
    case 0x7: return (uint8_t)(c_[sel_].addr >> 16);
    case 0x8: return (uint8_t)(cmd_ >> 8);
    case 0x9: return (uint8_t)cmd_;
    case 0xA: { uint8_t s = status_; status_ = 0; return s; }
    case 0xB: return c_[sel_].mode;
    case 0xE: return mask_;
    case 0xF: return req_;
    default: return 0;
    }
}
uint16_t Dmac::read16(uint32_t off) {
    switch (off & 0xF) {
    case 0x2: return (uint16_t)c_[sel_].count;
    case 0x4: return (uint16_t)c_[sel_].addr;
    case 0x8: return cmd_;
    default: return (uint16_t)((read8(off) << 8) | read8(off + 1));
    }
}
void Dmac::write8(uint32_t off, uint8_t v) {
    switch (off & 0xF) {
    case 0x0: case 0x1: sel_ = v & 3; break;
    case 0x2: c_[sel_].count = (c_[sel_].count & 0xFF) | (v << 8); break;
    case 0x3: c_[sel_].count = (c_[sel_].count & 0xFF00) | v; break;
    case 0x4: c_[sel_].addr = (c_[sel_].addr & 0xFF00FF) | ((uint32_t)v << 8); break;
    case 0x5: c_[sel_].addr = (c_[sel_].addr & 0xFFFF00) | v; break;
    case 0x7: c_[sel_].addr = (c_[sel_].addr & 0x00FFFF) | ((uint32_t)v << 16); break;
    case 0x8: cmd_ = (uint16_t)((cmd_ & 0xFF) | (v << 8)); break;
    case 0x9: cmd_ = (uint16_t)((cmd_ & 0xFF00) | v); break;
    case 0xB: c_[sel_].mode = v; c_[sel_].tc = false; LOGT("%s: ch%d mode %02x\n", nm_, sel_, v); break;
    case 0xE: mask_ = v & 0xF; LOGT("%s: mask %x\n", nm_, mask_); break;
    case 0xF: req_ = v & 0xF; break;
    default: break;
    }
}
void Dmac::write16(uint32_t off, uint16_t v) {
    switch (off & 0xF) {
    case 0x2: c_[sel_].count = v; c_[sel_].tc = false; LOGT("%s: ch%d count %u\n", nm_, sel_, v + 1); break;
    case 0x4: c_[sel_].addr = (c_[sel_].addr & 0xFF0000) | v; LOGT("%s: ch%d addr %06x\n", nm_, sel_, c_[sel_].addr); break;
    case 0x8: cmd_ = v; LOGT("%s: command %04x\n", nm_, v); break;
    default: write8(off, (uint8_t)(v >> 8)); write8(off + 1, (uint8_t)v); break;
    }
}
void Dmac::step(int ch) {
    Ch& c = c_[ch];
    c.addr = (c.addr + 1) & 0xFFFFFF;
    if (c.count == 0) { c.tc = true; status_ |= (uint8_t)(1 << ch); mask_ |= (uint8_t)(1 << ch); LOGD("%s: ch%d terminal count\n", nm_, ch); if (on_tc) on_tc(ch); }
    else c.count--;
}
bool Dmac::dev_write_byte(int ch, uint8_t b) {
    if (!channel_ready(ch)) return false;
    bus_->write8(address(ch), b); step(ch); return true;
}
bool Dmac::dev_read_byte(int ch, uint8_t* b) {
    if (!channel_ready(ch)) return false;
    *b = bus_->read8(address(ch)); step(ch); return true;
}
