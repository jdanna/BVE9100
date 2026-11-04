#pragma once
#include "../bus.h"
#include "pit8254.h"
// SPF-4 sync / reference / GPI board at 0x0FF01000-0x0FF01FFF (and 0x0FFF1000 alias):
//   0x0FF01001/03/05/07  8254 #1 (odd bytes): counter 0 = GPI-0 pulse timer (16 kHz clock, mode 4)
//   0x0FF01201/03/05/07  8254 #2 (odd bytes): counters 0-2 = GPI-1..3 pulse timers
//   0x0FF01E01 LED, 0x0FF01E03 GPI trigger latch (bit 3 = GPI-0, bits 0-2 = GPI-1..3), 0x0FF01E07 control latch, 0x0FF01E05 mode (colour-frame select)
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
    bool gpi_on_[4] = {false, false, false, false};
    Pit8254& gpi_timer(int port) { return port == 0 ? pit1_ : pit2_; }
    static int gpi_ctr(int port) { return port == 0 ? 0 : port - 1; }
    void gpi_end(int port);
    uint8_t latch_[0x1000] = {0};
    double field_cycles_, acc_ = 0; int field_ = 0, cf_ = 0;
};
