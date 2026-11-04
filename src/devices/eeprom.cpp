#include "eeprom.h"
Eeprom::Eeprom(const std::string& path) : path_(path), mem_(0x800, 0xFF) {
    if (!path.empty()) { FILE* f = fopen(path.c_str(), "rb"); if (f) { fread(mem_.data(), 1, mem_.size(), f); fclose(f); LOGI("EEPROM: loaded %s\n", path.c_str()); } }
}
Eeprom::~Eeprom() { save(); }
void Eeprom::save() {
    if (path_.empty() || !dirty_) return;
    FILE* f = fopen(path_.c_str(), "wb"); if (!f) return; fwrite(mem_.data(), 1, mem_.size(), f); fclose(f); dirty_ = false;
}
