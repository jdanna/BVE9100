#pragma once
#include "../bus.h"
// WD33C93 SCSI controller at 0x0F009000 (+0 address/aux-status register, +1 data register).
// No BKE-9107 hard disk is attached: commands complete with "selection timeout" (0x42), RESET
// completes with status 0x00. Interrupt -> ICU0 IRQ1 (vector 0x51, kernel hdkirq).
class ScsiWd33c93 : public Device {
public:
    const char* name() const override { return "WD33C93"; }
    uint8_t read8(uint32_t off) override;
    void write8(uint32_t off, uint8_t v) override;
    void tick(uint32_t cycles) override;
    void reset() override { addr_ = 0; for (auto& r : regs_) r = 0; int_ = false; busy_ = false; timer_ = 0; if (on_irq) on_irq(false); }
    std::function<void(bool)> on_irq;
private:
    uint8_t addr_ = 0; uint8_t regs_[32] = {0};
    bool int_ = false, busy_ = false; int64_t timer_ = 0; uint8_t pending_status_ = 0;
};
