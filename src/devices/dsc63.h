#pragma once
#include "../bus.h"
#include "../irq.h"
#include "mailbox.h"
// DSC-63 display / keyboard / printer board.
//   0x0D000000-0x0D00FFFF  text VRAM: 80 x 29 cells, 0x140 bytes per row, 4 bytes per cell:
//                          word0 = 0x5000 | font<<8 | char,  word1 = color | blink<<14 | reverse<<15
//                          (0x0D002440.. is used as the second scroll partition buffer)
//   0x0D010000-0x0D01BFFF  pop-up / second plane ("fillppup")
//   0x0D01C800-0x0D01CFFF  CRT controller registers:
//      C980/82/84/86 (w) partition start addresses ((vram_addr - 0x0D000000) >> 2)
//      C990/92/94/96 (w) partition line counts
//      C9A0 (w) cursor address, C9A2 (w) cursor shape/control, C9A4 (w) cursor misc
//      C9C0 (w) control (0x8000 = display on), C9C8 (r) status bit0 = busy
//      CA00-CAFF keyboard mailbox, CC00-CCFF printer mailbox, CFFE (w) "update" strobe
class Dsc63 : public Device {
public:
    Dsc63(IrqController* irq);
    const char* name() const override { return "DSC63"; }
    uint8_t read8(uint32_t off) override;
    void write8(uint32_t off, uint8_t v) override;
    uint16_t read16(uint32_t off) override;
    void write16(uint32_t off, uint16_t v) override;
    void tick(uint32_t cycles) override;
    void reset() override;
    MailboxPort keyboard, printer;
    // Render the visible 80x29 text screen as plain text lines (for the console front-end)
    std::vector<std::string> render_text() const;
    void dump_screen(FILE* out, bool color) const;
    void save_vram(const std::string& path) const;   // raw VRAM + plane2 + registers for offline analysis
    std::string screen_json() const;                  // 29 rows x 80 cells of [char, font, attr] (pop-up plane merged)
    uint32_t partition_start() const { return (((uint32_t)regs_[0x180] << 8) | regs_[0x181]) << 2; }
    bool screen_dirty = false;
    uint8_t kbd_type = 0x43;                 // value answered to the keyboard 'R','Z' request (type/version byte)
    uint8_t keyboard_read_reg(uint8_t reg);
private:
    std::vector<uint8_t> vram_, plane2_;
    uint8_t regs_[0x800] = {0};
};
