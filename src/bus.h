#pragma once
#include "common.h"

// A memory-mapped device. Offsets passed in are relative to the mapping base.
class Device {
public:
    virtual ~Device() {}
    virtual const char* name() const = 0;
    virtual uint8_t  read8(uint32_t off) = 0;
    virtual void     write8(uint32_t off, uint8_t v) = 0;
    virtual uint16_t read16(uint32_t off)            { return (uint16_t)((read8(off) << 8) | read8(off + 1)); }
    virtual void     write16(uint32_t off, uint16_t v) { write8(off, (uint8_t)(v >> 8)); write8(off + 1, (uint8_t)v); }
    virtual uint32_t read32(uint32_t off)            { return ((uint32_t)read16(off) << 16) | read16(off + 2); }
    virtual void     write32(uint32_t off, uint32_t v) { write16(off, (uint16_t)(v >> 16)); write16(off + 2, (uint16_t)v); }
    // Called periodically with the number of CPU cycles elapsed.
    virtual void tick(uint32_t cycles) {}
    virtual void reset() {}
    // Direct pointer into backing store (RAM only), for DMA/loader fast paths.
    virtual uint8_t* ptr(uint32_t off, uint32_t len) { return nullptr; }
};

class Ram : public Device {
public:
    Ram(const char* nm, uint32_t size) : nm_(nm), mem_(size, 0) {}
    const char* name() const override { return nm_; }
    uint8_t  read8(uint32_t o) override { return mem_[o]; }
    void     write8(uint32_t o, uint8_t v) override { mem_[o] = v; }
    uint16_t read16(uint32_t o) override { return (uint16_t)((mem_[o] << 8) | mem_[o + 1]); }
    void     write16(uint32_t o, uint16_t v) override { mem_[o] = (uint8_t)(v >> 8); mem_[o + 1] = (uint8_t)v; }
    uint32_t read32(uint32_t o) override { return ((uint32_t)mem_[o] << 24) | ((uint32_t)mem_[o+1] << 16) | ((uint32_t)mem_[o+2] << 8) | mem_[o+3]; }
    void     write32(uint32_t o, uint32_t v) override { mem_[o] = (uint8_t)(v >> 24); mem_[o+1] = (uint8_t)(v >> 16); mem_[o+2] = (uint8_t)(v >> 8); mem_[o+3] = (uint8_t)v; }
    uint8_t* ptr(uint32_t o, uint32_t len) override { return (o + len <= mem_.size()) ? &mem_[o] : nullptr; }
    uint32_t size() const { return (uint32_t)mem_.size(); }
    std::vector<uint8_t>& data() { return mem_; }
private:
    const char* nm_;
    std::vector<uint8_t> mem_;
};

struct Mapping { uint32_t base, size; Device* dev; };

// 32-bit physical address bus of the CPU-88 board.
class Bus {
public:
    void map(uint32_t base, uint32_t size, Device* dev) { maps_.push_back({base, size, dev}); add_device(dev); }
    Device* find(uint32_t addr, uint32_t* off) const {
        for (const auto& m : maps_)
            if (addr >= m.base && addr - m.base < m.size) { *off = addr - m.base; return m.dev; }
        return nullptr;
    }
    uint8_t  read8(uint32_t a);
    uint16_t read16(uint32_t a);
    uint32_t read32(uint32_t a);
    void     write8(uint32_t a, uint8_t v);
    void     write16(uint32_t a, uint16_t v);
    void     write32(uint32_t a, uint32_t v);
    // Copy helpers (bypass device side effects where RAM is involved)
    void write_block(uint32_t a, const uint8_t* src, uint32_t len) { for (uint32_t i = 0; i < len; i++) write8(a + i, src[i]); }
    // Devices may be mapped at several addresses; tick/reset each one exactly once.
    void add_device(Device* d) { for (auto* x : devs_) if (x == d) return; devs_.push_back(d); }
    void tick(uint32_t cycles) { for (auto* d : devs_) d->tick(cycles); }
    void reset() { for (auto* d : devs_) d->reset(); }

    // Unmapped access policy: when true a bus error is raised (68030 behaviour for empty slots).
    bool bus_error_on_unmapped = false;
    std::function<void(uint32_t addr, bool write)> on_unmapped;   // callback (raise bus error / log)
    const std::vector<Mapping>& mappings() const { return maps_; }
private:
    void unmapped(uint32_t a, bool w);
    std::vector<Mapping> maps_;
    std::vector<Device*> devs_;
};
