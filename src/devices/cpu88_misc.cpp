#include "cpu88_misc.h"
uint8_t Cpu88Misc::read8(uint32_t off) {
    switch (off) {
    case 0xB000: return fdc_->write_protected() ? 0x40 : 0x00;   // polarity guess: bit set = protected
    case 0xB001: return fdctl_;
    case 0xB002: return dipsw;
    case 0xB003: return leds;
    case 0xB004: return (uint8_t)(0xFF & ~(dchg_[fdsel_ & 1] ? 0x02 : 0x00));
    default:
        if (off >= 0xB000 && off < 0xB010) return regs_[off & 0xF];
        return 0xFF;
    }
}
void Cpu88Misc::write8(uint32_t off, uint8_t v) {
    switch (off) {
    case 0x0000: case 0x0001: LOGD("CPU88: reset strobe +%x = %02x\n", off, v); break;
    case 0xB000: fdsel_ = v; fdc_->select_drive(v & 1); LOGT("CPU88: FDD select %02x\n", v); break;
    case 0xB001: if ((v & 0x10) && !(fdctl_ & 0x10)) dchg_[fdsel_ & 1] = false;   // clear disk-change latch
        fdctl_ = v; fdc_->select_side((v >> 6) & 1); fdc_->set_density(v & 0x02); LOGT("CPU88: FDD ctl %02x\n", v); break;
    case 0xB003: if (v != leds) { leds = v; LOGD("CPU88: LEDs/7seg = %02x\n", v); if (on_leds) on_leds(v); } break;
    case 0xC000: dma_->set_page(v); break;
    case 0xD000: LOGD("CPU88: SCSI reset %02x\n", v); break;
    default:
        if (off >= 0xB000 && off < 0xB010) { regs_[off & 0xF] = v; LOGD("CPU88: latch %04x = %02x\n", off, v); }
        else LOGD("CPU88: write %04x = %02x\n", off, v);
    }
}
