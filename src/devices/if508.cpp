#include "if508.h"
If508::If508(IrqController* irq) : mem_(0x2000, 0), irq_(irq) {
    irq_id_ = irq->add("IF508", 2, [this]() { irq_pending_ = false; irq_->set(irq_id_, false); return (int)mem_[0x19D9]; });
    reset();
}
void If508::reset() {
    std::fill(mem_.begin(), mem_.end(), 0);
    wr16(0x0010, 0xFFFF);                 // board booted
    wr16(0x19C4, 0x1000);                 // netsio channel ready
    // 0x0FF03FF8 = 0xFF050800 is written by the board firmware once it has booted and linked to the Indy host;
    // the application then expects answers from the network manager task. Leave it clear (no firmware/Indy).
    cmd_timer_ = tx_timer_ = 0;
}
void If508::raise() { irq_pending_ = true; irq_->set(irq_id_, true); }
uint8_t If508::read8(uint32_t off) { return mem_[off & 0x1FFF]; }
void If508::write8(uint32_t off, uint8_t v) {
    off &= 0x1FFF;
    mem_[off] = v;
    if (off == 0x0011) {                                 // command word written (low byte last)
        uint16_t cmd = rd16(0x0010);
        if (cmd & 0x8000) { LOGD("IF508: host command %04x\n", cmd); cmd_timer_ = 4000; }
        else if (cmd == 0) { wr16(0x0000, 0x0008); LOGD("IF508: host acknowledged boot, board reports ready\n"); }   // if508rdy polls word 0
    }
    if (off == 0x19C1) {                                 // netsio command
        uint16_t c = rd16(0x19C0);
        LOGD("IF508: netsio cmd %04x (tx count %u)\n", c, rd16(0x19DC));
        if (c & 0x0004) tx_timer_ = 20000;               // send: consume TX buffer shortly
        if (c & 0x1000) { wr16(0x19E0, 0); }             // RX consumed
        wr16(0x19C0, 0);
    }
}
void If508::tick(uint32_t cycles) {
    if (cmd_timer_ > 0 && (cmd_timer_ -= cycles) <= 0) { cmd_timer_ = 0; wr16(0x0010, 0); }   // command accepted
    if (tx_timer_ > 0 && (tx_timer_ -= cycles) <= 0) {
        tx_timer_ = 0;
        uint16_t n = rd16(0x19DC);
        std::vector<uint8_t> blk(mem_.begin() + 0x1A00, mem_.begin() + 0x1A00 + (n > 0x100 ? 0x100 : n));
        LOGD("IF508: netsio TX %u bytes\n", n);
        if (on_serial_tx) on_serial_tx(blk);
        wr16(0x19DC, 0);                                 // sent
        raise();
    }
}
