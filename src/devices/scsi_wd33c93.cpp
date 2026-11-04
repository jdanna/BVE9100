#include "scsi_wd33c93.h"
uint8_t ScsiWd33c93::read8(uint32_t off) {
    if ((off & 1) == 0) return (uint8_t)((int_ ? 0x80 : 0) | (busy_ ? 0x20 : 0));   // aux status: INT, BSY
    uint8_t r = addr_ & 0x1F;
    uint8_t v = regs_[r];
    if (r == 0x17) { if (int_) { int_ = false; if (on_irq) on_irq(false); } }   // reading SCSI status clears INT
    if (r < 0x18) addr_ = (addr_ + 1) & 0x1F;
    return v;
}
void ScsiWd33c93::write8(uint32_t off, uint8_t v) {
    if ((off & 1) == 0) { addr_ = v & 0x1F; return; }
    uint8_t r = addr_ & 0x1F;
    regs_[r] = v;
    if (r == 0x18) {
        uint8_t cmd = v & 0x7F;
        busy_ = true;
        if (cmd == 0x00) { pending_status_ = 0x00; timer_ = 20000; LOGD("WD33C93: reset\n"); }               // reset complete
        else if (cmd == 0x01) { busy_ = false; }                                                               // abort
        else { pending_status_ = 0x42; timer_ = 200000; LOGD("WD33C93: command %02x -> selection timeout (no target)\n", cmd); }
    } else if (r < 0x18) addr_ = (addr_ + 1) & 0x1F;
}
void ScsiWd33c93::tick(uint32_t cycles) {
    if (timer_ > 0 && (timer_ -= cycles) <= 0) {
        timer_ = 0; busy_ = false; regs_[0x17] = pending_status_; int_ = true;
        if (on_irq) on_irq(true);
    }
}
