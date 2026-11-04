#include "mailbox.h"
MailboxPort::MailboxPort(const char* nm, IrqController* irq, int level, uint32_t window)
    : nm_(nm), irq_(irq), win_(window), mem_(window, 0) {
    irq_id_ = irq->add(nm_, level, [this]() {
        uint8_t v = pending_vecs_.empty() ? vec_nop : pending_vecs_.front();
        if (!pending_vecs_.empty()) pending_vecs_.erase(pending_vecs_.begin());
        if (pending_vecs_.empty()) irq_->set(irq_id_, false);
        return (int)v;
    });
}
void MailboxPort::reset() { std::fill(mem_.begin(), mem_.end(), 0); pending_vecs_.clear(); rxq_.clear(); cmd_timer_ = tx_timer_ = 0; }
void MailboxPort::raise(uint8_t vec) { if (!vec) return; pending_vecs_.push_back(vec); irq_->set(irq_id_, true); }
uint8_t MailboxPort::read8(uint32_t off) {
    off &= win_ - 1;
    if (off == 0x20) return 0;
    if (off == 0x21) return (uint8_t)((cmd_timer_ > 0 || tx_timer_ > 0) ? 0 : 1);   // bit0: sub-CPU idle
    return mem_[off];
}
void MailboxPort::write8(uint32_t off, uint8_t v) {
    off &= win_ - 1;
    mem_[off] = v;
    if (off == 0x01 && (mem_[0] | mem_[1])) { cmd_timer_ = 20000; LOGD("%s: command %02x%02x p1=%02x%02x p2=%02x%02x\n", nm_.c_str(), mem_[0], mem_[1], mem_[2], mem_[3], mem_[4], mem_[5]); }
    if (off == 0x0B) { vec_tx = mem_[0x0A]; vec_rx = mem_[0x0B]; LOGD("%s: vectors tx=%02x rx=%02x\n", nm_.c_str(), vec_tx, vec_rx); }
    if (off == 0x0D) { vec_nop = mem_[0x0C]; vec_reset = mem_[0x0D]; LOGD("%s: vectors nop=%02x reset=%02x\n", nm_.c_str(), vec_nop, vec_reset); }
    if (off == 0x41 && (mem_[0x40] | mem_[0x41])) tx_timer_ = 20000;
}
void MailboxPort::tick(uint32_t cycles) {
    if (cmd_timer_ > 0 && (cmd_timer_ -= cycles) <= 0) {
        cmd_timer_ = 0; mem_[0] = mem_[1] = 0;            // command accepted: completion is signalled via status bit0 only.
        // NOTE: the "reset" vector means "sub-CPU restarted, please re-initialise me"; raising it after
        // every command makes the SRMS printer/keyboard drivers re-init forever (observed).
    }
    if (tx_timer_ > 0 && (tx_timer_ -= cycles) <= 0) {
        tx_timer_ = 0;
        int n = (mem_[0x40] << 8) | mem_[0x41];
        if (n > (int)win_ - 0x42) n = (int)win_ - 0x42;
        std::vector<uint8_t> blk(mem_.begin() + 0x42, mem_.begin() + 0x42 + n);
        mem_[0x40] = mem_[0x41] = 0;
        LOGD("%s: host TX %d bytes:", nm_.c_str(), n);
        if (g_verbose >= 2) { for (auto b : blk) fprintf(stderr, " %02x", b); fprintf(stderr, "\n"); }
        if (on_tx) on_tx(blk);
        raise(vec_tx);
    }
    // feed queued RX bytes when the host has drained the previous block
    if (!rxq_.empty() && mem_[0xE0] == 0 && mem_[0xE1] == 0) {
        int n = (int)rxq_.size(); if (n > 16) n = 16;
        for (int i = 0; i < n; i++) mem_[0xE2 + i] = rxq_[i];
        rxq_.erase(rxq_.begin(), rxq_.begin() + n);
        mem_[0xE0] = 0; mem_[0xE1] = (uint8_t)n;
        raise(vec_rx);
    }
}
void MailboxPort::deliver(const std::vector<uint8_t>& bytes) { rxq_.insert(rxq_.end(), bytes.begin(), bytes.end()); }
