#pragma once
#include "../bus.h"
// Intel 8259A interrupt controller (single mode, edge triggered as programmed by SRMS:
// ICW1=0x13, ICW2=base, ICW4=0x0F). Registers: +0 = ICW1/OCW2/OCW3, +1 = ICW2..4/OCW1.
class Pic8259 : public Device {
public:
    Pic8259(const char* nm) : nm_(nm) {}
    const char* name() const override { return nm_; }
    uint8_t read8(uint32_t off) override;
    void write8(uint32_t off, uint8_t v) override;
    void reset() override;
    void set_irq(int n, bool level);          // device line n (0-7)
    bool int_out() const;                     // INT pin state
    int acknowledge();                        // IACK: returns vector, moves IRR->ISR
    std::function<void(bool)> on_int;         // INT pin change
    uint8_t base() const { return base_; }
private:
    void update();
    const char* nm_;
    uint8_t irr_ = 0, isr_ = 0, imr_ = 0xFF, base_ = 0, lines_ = 0;
    int icw_ = 0; bool icw4_ = false, aeoi_ = false, read_isr_ = false, poll_ = false;
    bool int_ = false;
    int pending_vector() const;
};
