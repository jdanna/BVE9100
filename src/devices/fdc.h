#pragma once
#include "../bus.h"
#include "dmac.h"
// WD177x-family floppy disk controller at 0x0F007000: +0 status/command, +1 track, +2 sector, +3 data.
// Drive select / side / density come from the CPU-88 latches at 0x0F00B000/0x0F00B001 (see Cpu88Misc).
// Data moves through DMAC channel 3. Completion raises IRQ (ICU0 IRQ2 -> vector 0x52).
class Fdc : public Device {
public:
    struct Drive {
        std::vector<uint8_t> image; std::string path; bool present = false, wp = false, dirty = false;
        int tracks = 80, heads = 2, spt = 18; int track = 0;
    };
    Fdc(Dmac* dma) : dma_(dma) {}
    const char* name() const override { return "FDC"; }
    uint8_t read8(uint32_t off) override;
    void write8(uint32_t off, uint8_t v) override;
    void tick(uint32_t cycles) override;
    void reset() override;
    bool load_image(int drive, const std::string& path, bool readonly);
    void save_images();
    // external latches
    void select_drive(int d) { sel_ = d & 1; }
    void select_side(int s) { side_ = s & 1; }
    void set_density(bool hd) { hd_ = hd; }
    bool write_protected() const { return drives_[sel_].present && drives_[sel_].wp; }
    bool disk_present(int d) const { return drives_[d & 1].present; }
    const std::vector<uint8_t>& image(int d) const { return drives_[d & 1].image; }
    std::function<void(bool)> on_irq;
    std::function<void(bool)> on_drq;
    int dma_channel = 3;
private:
    void start_command(uint8_t cmd);
    void finish(uint8_t status);
    void do_type1(uint8_t cmd);
    void xfer_sector();
    uint8_t* sector_ptr(Drive& d, int track, int side, int sector);
    Dmac* dma_;
    Drive drives_[2];
    int sel_ = 0, side_ = 0; bool hd_ = true;
    uint8_t status_ = 0, track_ = 0, sector_ = 1, data_ = 0, cmd_ = 0;
    bool busy_ = false, irq_ = false, type1_ = true;
    int64_t timer_ = 0;   // cycles until the pending command completes
    int pending_ = 0;     // 1 = type I done, 2 = read sector, 3 = write sector, 4 = read address
    int multi_ = 0; bool dir_ = true;
};
