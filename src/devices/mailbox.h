#pragma once
#include "../bus.h"
#include "../irq.h"
// Shared-memory "mailbox" port served by a sub-processor. Used for the DSC-63 keyboard channel
// (0x0D01CA00), the DSC-63 printer channel (0x0D01CC00) and the four SIO-4 RS-232C channels
// (0x0FFFE000 + n*0x400). Register layout (host view, offsets within the 0x100/0x400 window):
//   +0x00 (w) command (6 = initialise channel), cleared by the sub-CPU when done
//   +0x02 (w) parameter 1,  +0x04 (w) parameter 2 (baud/format)
//   +0x0A (w) interrupt vectors: hi = TX done, lo = RX data
//   +0x0C (w) interrupt vectors: hi = "nop", lo = reset/complete
//   +0x20 (w) status: bit0 = 1 when the sub-CPU is idle/ready (host polls this before and after a command)
//   +0x40 (w) TX byte count, +0x42.. TX buffer (up to 0x9E bytes)
//   +0xE0 (w) RX byte count, +0xE2.. RX buffer (16 bytes)
// The interrupt vectors are supplied by the port when it interrupts the 68030.
class MailboxPort : public Device {
public:
    MailboxPort(const char* nm, IrqController* irq, int level, uint32_t window = 0x100);
    const char* name() const override { return nm_.c_str(); }
    uint8_t read8(uint32_t off) override;
    void write8(uint32_t off, uint8_t v) override;
    void tick(uint32_t cycles) override;
    void reset() override;
    // Deliver bytes from the attached peripheral (keyboard, serial line) to the host.
    void deliver(const std::vector<uint8_t>& bytes);
    // Called when the host hands the sub-CPU a TX block (command bytes for the peripheral).
    std::function<void(const std::vector<uint8_t>&)> on_tx;
    // Default vectors used before the host programs +0x0A/+0x0C
    uint8_t vec_rx = 0, vec_tx = 0, vec_reset = 0, vec_nop = 0;
private:
    void raise(uint8_t vec);
    std::string nm_; IrqController* irq_; int irq_id_; uint32_t win_;
    std::vector<uint8_t> mem_;
    std::vector<uint8_t> pending_vecs_;
    std::vector<uint8_t> rxq_;
    int64_t cmd_timer_ = 0, tx_timer_ = 0;
};
