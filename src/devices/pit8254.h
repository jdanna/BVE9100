#pragma once
#include "../bus.h"
// Intel 8254 programmable interval timer. Registers: +0..+2 counters, +3 control.
// The CPU-88 maps the two on-board PITs byte-wide at 0x0F002000 (PIT0) and 0x0F005000 (PIT1);
// the SPF board carries two more on odd byte addresses (stride 2).
class Pit8254 : public Device {
public:
    Pit8254(const char* nm, double clock_hz, int stride = 1) : nm_(nm), clk_(clock_hz), stride_(stride) {}
    const char* name() const override { return nm_; }
    uint8_t read8(uint32_t off) override;
    void write8(uint32_t off, uint8_t v) override;
    void tick(uint32_t cycles) override;
    void reset() override;
    // OUT pin edge callbacks (called on the rising edge of OUTn)
    std::function<void(int ch, bool level)> on_out;
    uint16_t count(int ch) const { return (uint16_t)c_[ch].count; }
    // GATE input (high unless a board wires it): low holds modes 0, 2, 3 and 4; a rising edge (re)starts modes 1 and 5
    void set_gate(int ch, bool g);
private:
    struct Counter {
        uint16_t reload = 0; int count = 0; int mode = 0; int rw = 3; bool bcd = false;
        bool loaded = false; bool armed = false; bool out = true;
        int wr_phase = 0, rd_phase = 0; uint16_t latch = 0; bool latched = false;
        bool null_count = true; bool gate = true;
    } c_[3];
    void load(int ch);
    void clock(int ch, int n);
    const char* nm_;
    double clk_;
    int stride_;
    double acc_ = 0;
};
