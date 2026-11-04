#include "fdc.h"
#include <cstring>

void Fdc::reset() { status_ = 0; track_ = 0; sector_ = 1; busy_ = false; pending_ = 0; timer_ = 0; if (irq_) { irq_ = false; if (on_irq) on_irq(false); } }

bool Fdc::load_image(int drive, const std::string& path, bool readonly) {
    Drive& d = drives_[drive & 1];
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { LOGI("FDC: cannot open %s\n", path.c_str()); return false; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    d.image.assign((size_t)sz, 0); fread(d.image.data(), 1, (size_t)sz, f); fclose(f);
    d.path = path; d.present = true; d.wp = readonly; d.dirty = false;
    if (sz == 1474560) { d.tracks = 80; d.heads = 2; d.spt = 18; }
    else if (sz == 737280) { d.tracks = 80; d.heads = 2; d.spt = 9; }
    else if (sz == 1261568) { d.tracks = 77; d.heads = 2; d.spt = 8; }   // 8" 1.2M (BKE-9510), 1024 B? treated as 512
    else { LOGI("FDC: unknown image size %ld, assuming 2HD geometry\n", sz); d.tracks = 80; d.heads = 2; d.spt = 18; }
    LOGI("FDC: drive %c <- %s (%d tracks x %d heads x %d sectors%s)\n", 'A' + (drive & 1), path.c_str(), d.tracks, d.heads, d.spt, readonly ? ", read-only" : "");
    return true;
}
void Fdc::save_images() {
    for (auto& d : drives_) if (d.present && d.dirty && !d.wp) {
        FILE* f = fopen(d.path.c_str(), "wb"); if (f) { fwrite(d.image.data(), 1, d.image.size(), f); fclose(f); d.dirty = false; LOGI("FDC: wrote back %s\n", d.path.c_str()); }
    }
}
uint8_t* Fdc::sector_ptr(Drive& d, int track, int side, int sector) {
    if (!d.present || track < 0 || track >= d.tracks || side >= d.heads || sector < 1 || sector > d.spt) return nullptr;
    size_t lba = ((size_t)track * d.heads + side) * d.spt + (sector - 1);
    if ((lba + 1) * 512 > d.image.size()) return nullptr;
    return &d.image[lba * 512];
}

uint8_t Fdc::read8(uint32_t off) {
    switch (off & 3) {
    case 0: {
        if (irq_) { irq_ = false; if (on_irq) on_irq(false); }
        uint8_t s = status_;
        if (busy_) s |= 0x01;
        if (type1_ && !busy_) {
            s &= 0xFE;
            if (!drives_[sel_].present) s |= 0x80;
            if (drives_[sel_].track == 0) s |= 0x04;
            if (drives_[sel_].wp) s |= 0x40;
            s |= 0x20;                       // head loaded
            s ^= (uint8_t)((g_cycles >> 14) & 0x02);   // index pulse toggling
        }
        return s;
    }
    case 1: return track_;
    case 2: return sector_;
    default: return data_;
    }
}
void Fdc::write8(uint32_t off, uint8_t v) {
    switch (off & 3) {
    case 0: start_command(v); break;
    case 1: track_ = v; break;
    case 2: sector_ = v; break;
    default: data_ = v; break;
    }
}
void Fdc::finish(uint8_t status) {
    status_ = status; busy_ = false; pending_ = 0;
    if (!irq_) { irq_ = true; if (on_irq) on_irq(true); }
}
void Fdc::start_command(uint8_t cmd) {
    if ((cmd & 0xF0) == 0xD0) {               // force interrupt
        busy_ = false; pending_ = 0; status_ &= 0xFE;
        if (cmd & 0x08) finish(status_);
        else if (irq_) { irq_ = false; if (on_irq) on_irq(false); }
        return;
    }
    if ((cmd & 0xF0) == 0xF0 && (cmd & 0x0E) == 0x0E) {
        // 0xFE: vendor extension used by SRMS before every access ("set mode", followed by a data byte 0x40 / 0x60
        // for 3.5" / 8" drives). Not a WD177x command; accept silently without an interrupt.
        LOGT("FDC: mode command %02x\n", cmd); return;
    }
    if (busy_) { LOGD("FDC: command %02x while busy, ignored\n", cmd); return; }
    cmd_ = cmd; busy_ = true; status_ = 0;
    Drive& d = drives_[sel_];
    type1_ = (cmd & 0x80) == 0;
    LOGT("FDC: cmd %02x drive %d side %d trk %d sec %d (phys trk %d)\n", cmd, sel_, side_, track_, sector_, d.track);
    if (type1_) { do_type1(cmd); return; }
    if (!d.present) { pending_ = 1; timer_ = 2000; status_ = 0x80; return; }
    switch (cmd & 0xE0) {
    case 0x80: pending_ = 2; multi_ = cmd & 0x10; timer_ = 4000; break;     // read sector
    case 0xA0: pending_ = 3; multi_ = cmd & 0x10; timer_ = 4000; break;     // write sector
    case 0xC0: pending_ = 4; timer_ = 2000; break;                          // read address
    default: LOGI("FDC: unimplemented command %02x\n", cmd); pending_ = 1; timer_ = 2000; status_ = 0x10; break;
    }
}
void Fdc::do_type1(uint8_t cmd) {
    Drive& d = drives_[sel_];
    int kind = cmd >> 4;
    int newtrk = d.track;
    switch (kind) {
    case 0: newtrk = 0; break;                                   // restore
    case 1: newtrk = data_; break;                               // seek to data register
    case 2: case 3: newtrk += (dir_ ? 1 : -1); break;            // step
    case 4: case 5: newtrk += 1; dir_ = true; break;             // step in
    case 6: case 7: newtrk -= 1; dir_ = false; break;            // step out
    }
    if (newtrk < 0) newtrk = 0; if (newtrk > 85) newtrk = 85;
    int moved = abs(newtrk - d.track);
    d.track = newtrk;
    if (kind == 0 || kind == 1 || (cmd & 0x10)) track_ = (uint8_t)newtrk;
    status_ = 0;
    if (!d.present) status_ |= 0x80;
    if ((cmd & 0x04) && d.present && newtrk >= d.tracks) status_ |= 0x10;   // verify: seek error
    pending_ = 1; timer_ = 1000 + 2000 * (int64_t)moved;        // ~ms-scale in 20 MHz cycles would be far longer; keep fast
}
void Fdc::xfer_sector() {
    Drive& d = drives_[sel_];
    uint8_t* p = sector_ptr(d, d.track, side_, sector_);
    if (!p || d.track != track_) { finish(0x10); LOGD("FDC: record not found trk %d/%d side %d sec %d\n", d.track, track_, side_, sector_); return; }
    if (pending_ == 2) {
        for (int i = 0; i < 512; i++) if (!dma_->dev_write_byte(dma_channel, p[i])) { finish(0x04); LOGD("FDC: lost data (DMA not ready) on read\n"); return; }
    } else {
        if (d.wp) { finish(0x40); return; }
        for (int i = 0; i < 512; i++) { uint8_t b; if (!dma_->dev_read_byte(dma_channel, &b)) { finish(0x04); return; } p[i] = b; }
        d.dirty = true;
    }
    if (multi_ && sector_ < d.spt) { sector_++; timer_ = 2000; return; }   // multi-sector: continue
    finish(0);
}
void Fdc::tick(uint32_t cycles) {
    if (!pending_) return;
    timer_ -= cycles;
    if (timer_ > 0) return;
    switch (pending_) {
    case 1: finish(status_); break;
    case 2: case 3: xfer_sector(); break;
    case 4: {   // read address: 6 ID bytes via DMA
        Drive& d = drives_[sel_];
        uint8_t id[6] = {(uint8_t)d.track, (uint8_t)side_, 1, 2, 0, 0};
        for (auto b : id) dma_->dev_write_byte(dma_channel, b);
        sector_ = (uint8_t)d.track; finish(0); break;
    }
    }
}
