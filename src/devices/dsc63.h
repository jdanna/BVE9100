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
    // Search dial of the BKE-9400A keyboard: the keyboard sub-CPU keeps a 16-bit up/down counter in the
    // keyboard mailbox RAM; the kernel's dialstat() reads the word at 0x0D01CA02 + (word at 0x0D01CBE0).
    // The value read is (mode word << 16) | counter; the mode word has bit 13 + mode set (0 SHTL, 1 JOG, 2 DMC,
    // set by the host with keyboard command 'D'), which MANCNT compares with its own dial mode.
    void dial_add(int counts);
    int16_t dial_count() const { return dial_; }
    int dial_mode() const { return dial_mode_; }
    // Render the visible 80x29 text screen as plain text lines (for the console front-end)
    std::vector<std::string> render_text() const;
    void dump_screen(FILE* out, bool color) const;
    void save_vram(const std::string& path) const;   // raw VRAM + plane2 + registers for offline analysis
    std::string screen_json() const;                  // 29 rows x 80 cells of [char, font, attr] (pop-up plane merged)
    std::vector<uint32_t> row_bases() const;      // VRAM byte address of each of the 29 display rows
    uint32_t partition_start() const { return (((uint32_t)regs_[0x180] << 8) | regs_[0x181]) << 2; }
    bool screen_dirty = false;
    uint8_t kbd_type = 0x43;                 // value answered to the keyboard 'R','Z' request (type/version byte)
    uint8_t keyboard_read_reg(uint8_t reg);
    // Load one colour (6-bit r, g, b) into the palette registers as the application does: main palette entry
    // (0x1C800 + 16 i) = [colour, half, reverse, reverse] with half = c / 2 and reverse = 63 - c; overlay palette
    // entry (0x1C900 + 4 i) = colour, except entry 0, which is the overlay's transparent colour.
    void set_palette(int i, uint8_t r, uint8_t g, uint8_t b);
private:
    std::vector<uint8_t> vram_, plane2_;
    uint8_t regs_[0x800] = {0};
    int16_t dial_ = 0;
    int dial_mode_ = 3;             // 0 SHTL, 1 JOG, 2 DMC, 3 off
    int lamp_src_ = 0, lamp_audio_ = 0;
};
