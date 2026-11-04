#pragma once
#include "../bus.h"
#include "../irq.h"
// IF-508 Ethernet board (MIPS sub-system) - high-level stand-in. Only the host-visible shared RAM
// window and the "netsio" channel handshake are emulated; the board firmware (IF508A/B.OBJ) and the
// Indy link are not. This is enough for the kernel to mount the board and for the application's
// serial-mode request on device 0x89 to complete.
//   0x0FF02000-0x0FF03FFF  8 KB dual-port RAM
//   0x0FF02000   board status word, bit3 = ready (polled by if508rdy)
//   0x0FF02002   probed by if508moun (write 0x5555, read back)
//   0x0FF02010   board<->host command word: 0xFFFF after board boot; host writes 0 to acknowledge,
//                later writes (cmd | 0x8000) to issue a command; board clears it when done
//   0x0FF039C0   netsio command (4 = send TX buffer, 0x1000 = RX buffer consumed, mode bits)
//   0x0FF039C4   netsio status: bit 0x1000 = channel ready
//   0x0FF039C8..D4 baud/char/stop/parity, 0x0FF039D8 host interrupt vector for this channel
//   0x0FF039DC   TX byte count (board clears when sent), 0x0FF03A00.. TX data
//   0x0FF039E0   RX byte count (board sets), 0x0FF03B00.. RX data
//   0x0FF03FF8   board ID magic 0xFF050800, 0x0FF03FFC doorbell
class If508 : public Device {
public:
    If508(IrqController* irq);
    const char* name() const override { return "IF508"; }
    uint8_t read8(uint32_t off) override;
    void write8(uint32_t off, uint8_t v) override;
    void tick(uint32_t cycles) override;
    void reset() override;
    std::function<void(const std::vector<uint8_t>&)> on_serial_tx;   // bytes the host sends on the netsio channel
private:
    uint16_t rd16(uint32_t o) const { return (uint16_t)((mem_[o] << 8) | mem_[o + 1]); }
    void wr16(uint32_t o, uint16_t v) { mem_[o] = (uint8_t)(v >> 8); mem_[o + 1] = (uint8_t)v; }
    void raise();
    std::vector<uint8_t> mem_;
    IrqController* irq_; int irq_id_;
    int64_t cmd_timer_ = 0, tx_timer_ = 0; bool irq_pending_ = false;
};
