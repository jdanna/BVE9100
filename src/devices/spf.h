#pragma once
#include "../bus.h"
#include "pit8254.h"
// SPF-4 sync / reference / GPI board at 0x0FF01000-0x0FF01FFF (and 0x0FFF1000 alias):
//   0x0FF01001/03/05/07  8254 #1 (odd bytes): GPI pulse timers (counter n, mode 5)
//   0x0FF01201/03/05/07  8254 #2 (odd bytes)
//   0x0FF01E01 LED, 0x0FF01E03 / 0x0FF01E07 GPI output / control latches, 0x0FF01E05 mode (colour-frame select)
//   0x0FF01E09 status: bit6 reference present (field tick enable), bit0 field, bits1-2 colour frame
//   0x0FF01FD1 / 0x0FF01FD3  status (reference video present, colour frame, field)
//   0x0FF01FD7  board ID: high nibble 1 = SPF-1/2, 3 = SPF-3, 4 = SPF-4 (assumed)
// Generates the field interrupt (ICU1 IRQ3 -> vector 0x5B) at the reference field rate.
class Spf : public Device {
public:
    Spf(bool pal = false);
    const char* name() const override { return "SPF"; }
    uint8_t read8(uint32_t off) override;
    void write8(uint32_t off, uint8_t v) override;
    void tick(uint32_t cycles) override;
    std::function<void()> on_field;      // called every field (59.94 Hz NTSC / 50 Hz PAL)
    std::function<void(int port, bool on)> on_gpi;
    uint8_t board_id = 0x40;
private:
    Pit8254 pit1_, pit2_;
    uint8_t latch_[0x1000] = {0};
    double field_cycles_, acc_ = 0; int field_ = 0, cf_ = 0;
};
