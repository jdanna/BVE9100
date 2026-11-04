#pragma once
#include "common.h"
// Sony SRMS ".abs" load module (see tools/absfile.py for the format description).
struct AbsModule {
    uint32_t text_addr = 0, data_addr = 0, task_id = 0, entry = 0, dataptr = 0; uint16_t flags = 0;
    std::vector<uint8_t> text, data;
    std::string version;
    struct Sym { uint32_t addr; uint8_t type; std::string name; };
    std::vector<Sym> symbols;
    uint32_t real_entry() const { return entry ? entry : text_addr + 0x22; }
};
bool load_abs(const std::string& path, AbsModule& m, std::string* err);
