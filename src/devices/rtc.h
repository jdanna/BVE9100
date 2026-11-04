#pragma once
#include "../bus.h"
// Real-time clock with 16 x 4-bit registers (RP5C01 / MSM6242 register layout):
// 0 sec1 1 sec10 2 min1 3 min10 4 hr1 5 hr10 6 weekday 7 day1 8 day10 9 mon1 A mon10 B yr1 C yr10
// D/E/F control. Mapped at 0x0F006000 (byte per register). Time comes from the host clock.
class Rtc : public Device {
public:
    const char* name() const override { return "RTC"; }
    uint8_t read8(uint32_t off) override;
    void write8(uint32_t off, uint8_t v) override;
private:
    uint8_t ctl_[3] = {0, 0, 0};
};
