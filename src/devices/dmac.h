#pragma once
#include "../bus.h"
// CPU-88 DMA controller (8237-like semantics with 16-bit registers; exact part unknown).
// Register map (from RMS.SYS/RMSBDY.SYS dmahset/fdma_*):
//   +0x00 (b) channel select for the address/count window
//   +0x02 (w) transfer count - 1 (selected channel)
//   +0x04 (w) address bits 15..0
//   +0x07 (b) address bits 23..16
//   +0x08 (w) command (bit 2 = controller disable); kernel writes 0x0230 at init
//   +0x0A (b) status: bit n = terminal count reached on channel n (read clears)
//   +0x0B (b) mode for selected channel: 0x44 = device->memory, 0x48 = memory->device
//   +0x0E (b) mask register (bit n = channel n masked)
//   +0x0F (b) software request register
//   Page latch at 0x0F00C000: 0xC0 | address bit 24 (selects 0x01xxxxxx DRAM)
// Channel assignment: 3 = floppy, (0..2 = SCSI/memory-to-memory, TBD).
class Dmac : public Device {
public:
    Dmac(const char* nm, Bus* bus) : nm_(nm), bus_(bus) {}
    const char* name() const override { return nm_; }
    uint8_t read8(uint32_t off) override;
    void write8(uint32_t off, uint8_t v) override;
    uint16_t read16(uint32_t off) override;
    void write16(uint32_t off, uint16_t v) override;
    void reset() override;
    void set_page(uint8_t v) { page_ = v; }
    // Device-side DMA requests. Return false when channel masked/disabled.
    bool dev_write_byte(int ch, uint8_t b);   // device -> memory
    bool dev_read_byte(int ch, uint8_t* b);   // memory -> device
    bool channel_ready(int ch) const { return !(mask_ & (1 << ch)) && !(cmd_ & 4) && !c_[ch].tc; }
    std::function<void(int ch)> on_tc;        // terminal count reached on channel
    uint32_t address(int ch) const { return c_[ch].addr | ((page_ & 1) << 24); }
private:
    struct Ch { uint32_t addr = 0; int count = 0; uint8_t mode = 0; bool tc = false; } c_[4];
    void step(int ch);
    const char* nm_; Bus* bus_;
    int sel_ = 0; uint16_t cmd_ = 0; uint8_t mask_ = 0xF, req_ = 0, status_ = 0, page_ = 0xC0;
};
