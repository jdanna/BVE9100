#pragma once
#include "../bus.h"
#include "fdc.h"
#include "dmac.h"
// CPU-88 miscellaneous latches:
//   0x0F000000/01  reset strobes (SIO reset = write 1 to +1)
//   0x0F00B000     FDD select: bit0 drive (0=A, 1=B), bit7 = motor/enable. Read: bit? write-protect (isprtctd)
//   0x0F00B001     FDD control: 0x40 side 1, 0x08 precomp (trk>59), 0x20|0x04 head load/ready, 0x02 HD (2HD) density
//   0x0F00B002     DIP switch (read). bit7 = 1 -> "RMS control vectors" (kernel keeps its own bus-error handlers)
//   0x0F00B003     LEDs (active low, bit0..7) / 7-segment display; also 0xFF at kernel start
//   0x0F00B004     FDD status (read, active low): bit1 = 0 -> disk-change latch set for the selected drive.
//                  Cleared by pulsing 0x0F00B001 bit4.
//   0x0F00B006/07  written 0x30 / 0x0F at kernel start (wait-state / bank config?)
//   0x0F00C000     DMA page latch (0xC0 | A24)
//   0x0F00D000     SCSI reset
class Cpu88Misc : public Device {
public:
    Cpu88Misc(Fdc* fdc, Dmac* dma) : fdc_(fdc), dma_(dma) {}
    const char* name() const override { return "CPU88"; }
    uint8_t read8(uint32_t off) override;
    void write8(uint32_t off, uint8_t v) override;
    uint8_t dipsw = 0x00;             // all switches ON (=0): NTSC, auto-select boot, auto start, boot-load, RMS control vectors
    uint8_t leds = 0xFF;
    std::function<void(uint8_t)> on_leds;
private:
    Fdc* fdc_; Dmac* dma_;
    uint8_t fdsel_ = 0, fdctl_ = 0, regs_[16] = {0};
    bool dchg_[2] = {false, false};
public:
    void set_disk_changed(int drive) { dchg_[drive & 1] = true; }
};
