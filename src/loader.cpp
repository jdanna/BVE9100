#include "loader.h"
static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
bool load_abs(const std::string& path, AbsModule& m, std::string* err) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { if (err) *err = "cannot open " + path; return false; }
    std::vector<uint8_t> b; fseek(f, 0, SEEK_END); b.resize((size_t)ftell(f)); fseek(f, 0, SEEK_SET);
    fread(b.data(), 1, b.size(), f); fclose(f);
    if (b.size() < 0x40 || be16(&b[0]) != 0x992D) {
        if (b.size() > 7 && b[2] == '-' && b[3] == 'l' && b[4] == 'h') { if (err) *err = path + " is LHA-compressed; unpack it first (lha x) or use tools/absfile.py"; return false; }
        if (err) *err = path + ": not an SRMS .abs module"; return false;
    }
    uint32_t symsz = be16(&b[2]), tsz = be32(&b[4]), dsz = be32(&b[8]);
    m.text_addr = be32(&b[0x14]); m.data_addr = be32(&b[0x18]);
    if (0x1C + tsz + dsz > b.size()) { if (err) *err = path + ": truncated"; return false; }
    m.text.assign(b.begin() + 0x1C, b.begin() + 0x1C + tsz);
    m.data.assign(b.begin() + 0x1C + tsz, b.begin() + 0x1C + tsz + dsz);
    m.task_id = be32(&m.text[0]); m.entry = be32(&m.text[4]); m.dataptr = be32(&m.text[8]); m.flags = be16(&m.text[0xC]);
    m.version.assign((const char*)&m.text[0x0E], 20);
    size_t so = 0x1C + tsz + dsz;
    for (size_t i = so; i + 16 <= b.size() && i + 16 <= so + symsz; i += 16) {
        uint32_t a = be32(&b[i]); uint8_t t = b[i + 4];
        if (!a && !t) break;
        std::string nm((const char*)&b[i + 5], 11); nm = nm.substr(0, nm.find('\0'));
        m.symbols.push_back({a, t, nm});
    }
    return true;
}
