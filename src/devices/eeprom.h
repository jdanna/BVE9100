#pragma once
#include "../bus.h"
// 2 KB byte-wide EEPROM at 0x0F00A000-0x0F00A7FF (setup data, boot drive at 0x7F9 bits 0-2,
// serial bit-rate strap at 0x7FF bit 0). Optionally persisted to a file.
class Eeprom : public Device {
public:
    Eeprom(const std::string& path);
    ~Eeprom();
    const char* name() const override { return "EEPROM"; }
    uint8_t read8(uint32_t off) override { return mem_[off & 0x7FF]; }
    void write8(uint32_t off, uint8_t v) override { mem_[off & 0x7FF] = v; dirty_ = true; }
    void save();
    const std::vector<uint8_t>& data() const { return mem_; }
private:
    std::string path_; std::vector<uint8_t> mem_; bool dirty_ = false;
};
