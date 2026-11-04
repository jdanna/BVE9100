#pragma once
#include "../bus.h"
// Intel 8251A USART: +0 data, +1 mode/command/status. Used for the CPU-88 debug console
// at 0x0F001000 (kernel "sio": rx -> ICU1 IRQ5, break -> IRQ6). TX bytes go to a sink callback.
class Usart8251 : public Device {
public:
    Usart8251(const char* nm) : nm_(nm) {}
    const char* name() const override { return nm_; }
    uint8_t read8(uint32_t off) override;
    void write8(uint32_t off, uint8_t v) override;
    void reset() override;
    void rx_byte(uint8_t b);                 // host -> emulated system
    std::function<void(uint8_t)> on_tx;      // emulated system -> host
    std::function<void(bool)> on_rxrdy;      // RxRDY pin
private:
    const char* nm_;
    int phase_ = 0;      // 0: expecting mode, 1: expecting command
    uint8_t mode_ = 0, cmd_ = 0;
    bool rx_full_ = false; uint8_t rx_ = 0;
};
