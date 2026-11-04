#include "pic8259.h"

void Pic8259::reset() { irr_ = isr_ = 0; imr_ = 0xFF; icw_ = 0; int_ = false; lines_ = 0; }

int Pic8259::pending_vector() const {
    uint8_t req = irr_ & ~imr_;
    for (int i = 0; i < 8; i++) {
        if (isr_ & (1 << i)) return -1;     // higher priority in service (fixed priority, not SFNM-rotated)
        if (req & (1 << i)) return i;
    }
    return -1;
}
void Pic8259::update() {
    bool n = pending_vector() >= 0;
    if (n != int_) { int_ = n; if (on_int) on_int(n); }
}
void Pic8259::set_irq(int n, bool level) {
    uint8_t bit = (uint8_t)(1 << n);
    bool was = lines_ & bit;
    if (level) lines_ |= bit; else lines_ &= (uint8_t)~bit;
    if (level && !was) irr_ |= bit;         // edge triggered
    update();
}
bool Pic8259::int_out() const { return int_; }
int Pic8259::acknowledge() {
    int i = pending_vector();
    if (i < 0) { LOGI("%s: spurious IACK\n", nm_); return base_ + 7; }
    irr_ &= (uint8_t)~(1 << i);
    if (!aeoi_) isr_ |= (uint8_t)(1 << i);
    update();
    return base_ + i;
}
uint8_t Pic8259::read8(uint32_t off) {
    if ((off & 1) == 0) return read_isr_ ? isr_ : irr_;
    return imr_;
}
void Pic8259::write8(uint32_t off, uint8_t v) {
    if ((off & 1) == 0) {
        if (v & 0x10) {                     // ICW1
            icw_ = 2; icw4_ = v & 1; imr_ = 0xFF; isr_ = irr_ = 0; read_isr_ = false;
            LOGD("%s: ICW1 %02x\n", nm_, v);
        } else if ((v & 0x18) == 0x08) {    // OCW3
            if (v & 2) read_isr_ = v & 1;
            poll_ = v & 4;
        } else {                            // OCW2
            int cmd = v >> 5;
            if (cmd == 1 || cmd == 5) {     // non-specific EOI
                for (int i = 0; i < 8; i++) if (isr_ & (1 << i)) { isr_ &= (uint8_t)~(1 << i); break; }
            } else if (cmd == 3 || cmd == 7) { isr_ &= (uint8_t)~(1 << (v & 7)); }  // specific EOI
            update();
        }
        return;
    }
    switch (icw_) {
    case 2: base_ = v & 0xF8; icw_ = icw4_ ? 4 : 0; LOGD("%s: vector base %02x\n", nm_, base_); break;
    case 3: icw_ = icw4_ ? 4 : 0; break;
    case 4: aeoi_ = v & 2; icw_ = 0; LOGD("%s: ICW4 %02x (AEOI %d)\n", nm_, v, aeoi_); break;
    default: imr_ = v; LOGT("%s: mask %02x\n", nm_, v); update(); break;
    }
}
