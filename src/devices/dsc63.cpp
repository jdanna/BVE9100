#include "dsc63.h"
extern "C" unsigned int m68k_get_reg(void* context, int reg);
#define M68K_REG_PC_ID 16
Dsc63::Dsc63(IrqController* irq) : keyboard("DSC63.KBD", irq, 3), printer("DSC63.PRT", irq, 3), vram_(0x10000, 0), plane2_(0xC000, 0) {
    keyboard.vec_rx = 0xA0; keyboard.vec_reset = 0xA1; keyboard.vec_tx = 0xA2; keyboard.vec_nop = 0xA3;
    printer.vec_rx = 0xA4; printer.vec_reset = 0xA5; printer.vec_tx = 0xA6; printer.vec_nop = 0xA3;
    // Keyboard (BKE-9400A) command blocks start with 0x82; the keyboard answers 0x83 (ACK) / 0x84 (NAK).
    // Block format: 82 <len> <cmd> <data...> <checksum>. Commands seen: 'W' (0x57) mode set, 'K' (0x4B) key
    // table download, 'R' (0x52) read a keyboard register/status. Replies: 83 = ACK, 84 = NAK,
    // 82 <byte> = one data byte (the kernel's krecvdev hands that byte back to the requesting task).
    keyboard.on_tx = [this](const std::vector<uint8_t>& blk) {
        if (blk.size() < 3 || blk[0] != 0x82) return;
        uint8_t cmd = blk[2];
        if (cmd == 0x52) { uint8_t reg = blk.size() > 3 ? blk[3] : 0; uint8_t v = keyboard_read_reg(reg); keyboard.deliver({0x82, v}); LOGD("KBD: read reg %02x -> %02x\n", reg, v); return; }
        if (cmd == 0x4C && blk.size() > 3) {
            // 'L' key lamps: bit 7 set = video source lamp (0x81 R, 0x82..0x87 P1..P6, 0x88 AUX1, 0x89 AUX2,
            // 0x8A BLACK, 0x80 none); bit 7 clear = audio lamps A1..A4 as a bit mask (bits 0..3)
            uint8_t v = blk[3];
            if (v & 0x80) lamp_src_ = v & 0x7F; else lamp_audio_ = v & 0x0F;
        } else if (cmd == 0x44 && blk.size() > 3) {
            // 'D' search dial mode + its lamps: 0x88 SHTL, 0x86 JOG, 0x8A DMC, 0x8C off; an optional data byte
            // presets the dial counter (SHTL / DMC start from the centre)
            uint8_t m = blk[3];
            dial_mode_ = m == 0x88 ? 0 : m == 0x86 ? 1 : m == 0x8A ? 2 : 3;
            if (blk.size() > 5) dial_ = (int16_t)(int8_t)blk[4];
            LOGD("KBD: dial mode %02x -> %d, counter %d\n", m, dial_mode_, dial_);
        }
        keyboard.deliver({0x83});
    };
}
// Keyboard "register" values returned to 'R' requests. 'Z' (0x5A) is read right after the key tables are
// loaded; the application compares the high byte of a keyboard-type word against 0x43 ('C').
uint8_t Dsc63::keyboard_read_reg(uint8_t reg) { if (reg == 0x5A) return kbd_type; return 0; }
void Dsc63::reset() { keyboard.reset(); printer.reset(); dial_ = 0; dial_mode_ = 3; lamp_src_ = 0; lamp_audio_ = 0; }
// The dial counter lives at mailbox offset 0x102 (0x0D01CB02); the offset word at 0x0D01CBE0 points at it.
static constexpr uint32_t DIAL_PTR = 0x1CBE0, DIAL_OFS = 0x100, DIAL_CNT = 0x1CA02 + DIAL_OFS;
void Dsc63::set_palette(int i, uint8_t r, uint8_t g, uint8_t b) {
    if (i < 0 || i > 15) return;
    uint8_t c[3] = {(uint8_t)(r & 63), (uint8_t)(g & 63), (uint8_t)(b & 63)};
    uint8_t* e = &regs_[i * 16];
    for (int k = 0; k < 3; k++) { e[k] = c[k]; e[4 + k] = c[k] >> 1; e[8 + k] = e[12 + k] = 63 - c[k]; }
    if (i) for (int k = 0; k < 3; k++) regs_[0x100 + i * 4 + k] = c[k];
    screen_dirty = true;
}
void Dsc63::dial_add(int counts) { dial_ = (int16_t)(dial_ + counts); }
void Dsc63::tick(uint32_t c) { keyboard.tick(c); printer.tick(c); }
uint8_t Dsc63::read8(uint32_t off) {
    if (off < 0x10000) return vram_[off];
    if (off < 0x1C000) return plane2_[off - 0x10000];
    if (off >= 0x1CA00 && off < 0x1CB00) return keyboard.read8(off - 0x1CA00);
    if (off >= 0x1CC00 && off < 0x1CD00) return printer.read8(off - 0x1CC00);
    if (off == DIAL_PTR) return (uint8_t)(DIAL_OFS >> 8);
    if (off == DIAL_PTR + 1) return (uint8_t)DIAL_OFS;
    if (off == DIAL_CNT - 2) return dial_mode_ < 3 ? (uint8_t)(0x20 << dial_mode_) : 0;   // mode word (high byte)
    if (off == DIAL_CNT - 1) return 0;
    if (off == DIAL_CNT) return (uint8_t)((uint16_t)dial_ >> 8);
    if (off == DIAL_CNT + 1) return (uint8_t)dial_;
    if (off >= 0x1C800) {
        if (off == 0x1C9C8 || off == 0x1C9C9) return 0;      // not busy
        return regs_[off - 0x1C800];
    }
    return 0xFF;
}
void Dsc63::write8(uint32_t off, uint8_t v) {
    if (off < 0x10000) { vram_[off] = v; screen_dirty = true; return; }
    if (off < 0x1C000) { plane2_[off - 0x10000] = v; return; }
    if (off >= 0x1CA00 && off < 0x1CB00) { keyboard.write8(off - 0x1CA00, v); return; }
    if (off >= 0x1CC00 && off < 0x1CD00) { printer.write8(off - 0x1CC00, v); return; }
    if (off >= 0x1C800) {
        regs_[off - 0x1C800] = v;
        if (off == 0x1C981) LOGD("DSC63: partition0 start <- %04x (pc %08x)\n", (regs_[0x180] << 8) | v, m68k_get_reg(nullptr, M68K_REG_PC_ID));
        if (off == 0x1CFFF) screen_dirty = true;
        else if (off >= 0x1C980 && off < 0x1C9D0) LOGT("DSC63: reg %05x = %02x\n", off, v);
    }
}
uint16_t Dsc63::read16(uint32_t off) { return (uint16_t)((read8(off) << 8) | read8(off + 1)); }
void Dsc63::write16(uint32_t off, uint16_t v) { write8(off, (uint8_t)(v >> 8)); write8(off + 1, (uint8_t)v); }

std::vector<std::string> Dsc63::render_text() const {
    std::vector<std::string> out;
    // Partition 0 start address (default 0): rows are 0x140 bytes
    std::vector<uint32_t> bases = row_bases();
    for (int row = 0; row < 29; row++) {
        std::string line;
        for (int col = 0; col < 80; col++) {
            uint32_t a = (bases[row] + col * 4) & 0xFFFF;
            const uint8_t* cell = &vram_[a];
            if (a + 3 < plane2_.size()) { const uint8_t* p = &plane2_[a]; if (p[1] != 0x20 && (p[0] | p[1])) cell = p; }
            uint8_t ch = cell[1], font = cell[0] & 0x0F;
            if (font == 0) line.push_back((ch >= 0x20 && ch < 0x7F) ? (char)ch : ch == 0 ? ' ' : ch == 0xC0 ? '*' : '.');   // 0xC0: frame line with the selected-deck star
            else if ((font == 2 || font == 6) && ch >= 0x80) line.push_back(ch < 0xC0 ? (char)(ch - 0x60) : ' ');   // double-width serif: left half, right half
            else if (ch >= 0xC0) line.push_back(' ');
            else line.push_back(font == 7 ? '%' : '#');
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        out.push_back(line);
    }
    return out;
}
void Dsc63::dump_screen(FILE* out, bool color) const {
    std::vector<uint32_t> bases = row_bases();
    fprintf(out, "+%s+\n", std::string(80, '-').c_str());
    for (int row = 0; row < 29; row++) {
        fputc('|', out);
        int last_attr = -1;
        for (int col = 0; col < 80; col++) {
            uint32_t a = (bases[row] + col * 4) & 0xFFFF;
            const uint8_t* cell = &vram_[a];
            // pop-up plane (0x0D010000) overlays the main plane where it holds a non-blank cell
            if (a + 3 < plane2_.size()) { const uint8_t* p = &plane2_[a]; if (p[1] != 0x20 && (p[0] | p[1])) cell = p; }
            uint8_t ch = cell[1];
            uint8_t font = cell[0] & 0x0F;          // character generator bank (0 = ASCII, others = graphics/large fonts)
            uint16_t attr = (uint16_t)((cell[2] << 8) | cell[3]);
            if (color && attr != last_attr) {
                int c = attr & 0xF; bool rev = attr & 0x8000, blink = attr & 0x4000;
                static const int pal[16] = {30, 34, 32, 36, 31, 35, 33, 37, 90, 94, 92, 96, 91, 95, 93, 97};
                fprintf(out, "\033[0;%d%s%sm", pal[c], rev ? ";7" : "", blink ? ";5" : "");
                last_attr = attr;
            }
            // CG bank 7: 0x00-0x7F halftone/graphics tiles, 0x80-0xBF left half of a double-width ASCII glyph
            // (code - 0x80 + 0x20), 0xC0-0xFF its right half. Other banks: unknown, shown as '#'.
            if (font == 0) fputc((ch >= 0x20 && ch < 0x7F) ? ch : ch == 0 ? ' ' : ch == 0xC0 ? '*' : '.', out);   // 0xC0: frame line with the selected-deck star
            else if (ch >= 0x80 && ch < 0xC0) fputc(ch - 0x80 + 0x20, out);
            else if (ch >= 0xC0) fputc(' ', out);
            else fputc(font == 7 ? '%' : '#', out);
        }
        if (color) fputs("\033[0m", out);
        fputs("|\n", out);
    }
    fprintf(out, "+%s+\n", std::string(80, '-').c_str());
}

void Dsc63::save_vram(const std::string& path) const {
    FILE* f = fopen(path.c_str(), "wb"); if (!f) return;
    fwrite(vram_.data(), 1, vram_.size(), f); fwrite(plane2_.data(), 1, plane2_.size(), f); fwrite(regs_, 1, sizeof regs_, f); fclose(f);
    LOGI("DSC63: VRAM dumped to %s (vram 0x10000, plane2 0xC000, regs 0x800)\n", path.c_str());
}

// Display rows come from up to four partitions: partition n starts at VRAM byte (word at 0x1C980+2n) << 2 and
// shows (word at 0x1C990+2n) rows; the partitions are stacked top to bottom (the application uses this to scroll
// a list under a fixed header, e.g. DEVICE ASSIGNMENT). With no row counts programmed, partition 0 fills the screen.
std::vector<uint32_t> Dsc63::row_bases() const {
    std::vector<uint32_t> b;
    for (int p = 0; p < 4 && b.size() < 29; p++) {
        uint32_t start = (((uint32_t)regs_[0x180 + 2 * p] << 8) | regs_[0x181 + 2 * p]) << 2;
        int n = (regs_[0x190 + 2 * p] << 8) | regs_[0x191 + 2 * p];
        for (int r = 0; r < n && b.size() < 29; r++) b.push_back((start + r * 0x140) & 0xFFFF);
    }
    uint32_t start = partition_start();
    if (b.empty()) for (int r = 0; r < 29; r++) b.push_back((start + r * 0x140) & 0xFFFF);
    while (b.size() < 29) b.push_back((b.back() + 0x140) & 0xFFFF);
    return b;
}

std::string Dsc63::screen_json() const {
    // Main plane (0x0D000000): cells [char, CG bank, attr]; attr bits 0-3 fg, 8-11 bg (main palette), 14 blink,
    // 15 reverse. Overlay plane (0x0D010000, same addressing): byte0 = CG bank, byte3 = colours (bits 0-3 fg,
    // 4-7 bg, overlay palette; bg 0 = transparent). A cell is transparent when bytes 0 and 3 are both zero.
    std::string out; out.reserve(48000);
    std::vector<uint32_t> bases = row_bases();
    std::string ov;
    out += "{\"rows\":[";
    for (int row = 0; row < 29; row++) {
        out += row ? ",[" : "[";
        for (int col = 0; col < 80; col++) {
            uint32_t a = (bases[row] + col * 4) & 0xFFFF;
            const uint8_t* cell = &vram_[a];
            char b[48]; snprintf(b, sizeof b, "%s[%d,%d,%d]", col ? "," : "", cell[1], cell[0] & 0x0F, (cell[2] << 8) | cell[3]);
            out += b;
            if (a + 3 < plane2_.size()) {
                const uint8_t* p = &plane2_[a];
                if (p[0] || p[3]) {
                    snprintf(b, sizeof b, "%s[%d,%d,%d,%d,%d]", ov.empty() ? "" : ",", row, col, p[1], p[0] & 0x0F, p[3]);
                    ov += b;
                }
            }
        }
        out += "]";
    }
    out += "],\"ov\":[" + ov + "]";
    { char b[128]; snprintf(b, sizeof b, ",\"lamps\":{\"src\":%d,\"audio\":%d,\"dial\":%d,\"count\":%d}", lamp_src_, lamp_audio_, dial_mode_, dial_); out += b; }
    char tail[128]; snprintf(tail, sizeof tail, ",\"cursor\":%u,\"ctl\":%u", (unsigned)(((regs_[0x1A0] << 8) | regs_[0x1A1])), (unsigned)((regs_[0x1C0] << 8) | regs_[0x1C1]));
    out += tail;
    auto c8 = [](uint8_t v) { return (unsigned)((v & 0x3F) * 255 / 63); };
    // Main palette (0x0D01C800): 16 entries x four 6-bit RGB triples [colour, half, reverse, reverse].
    bool loaded = false;
    for (int i = 0; i < 0x100; i++) if (regs_[i]) { loaded = true; break; }
    if (loaded) {
        out += ",\"pal\":[";
        for (int i = 0; i < 16; i++) {
            char b[96]; const uint8_t* e = &regs_[i * 16];
            snprintf(b, sizeof b, "%s[%u,%u,%u,%u,%u,%u]", i ? "," : "", c8(e[0]), c8(e[1]), c8(e[2]), c8(e[8]), c8(e[9]), c8(e[10]));
            out += b;
        }
        out += "]";
    }
    // Overlay palette (0x0D01C900): 16 x [r, g, b, 0]
    out += ",\"ovpal\":[";
    for (int i = 0; i < 16; i++) {
        char b[48]; const uint8_t* e = &regs_[0x100 + i * 4];
        snprintf(b, sizeof b, "%s[%u,%u,%u]", i ? "," : "", c8(e[0]), c8(e[1]), c8(e[2]));
        out += b;
    }
    out += "]}";
    return out;
}
