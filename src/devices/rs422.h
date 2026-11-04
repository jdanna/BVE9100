#pragma once
#include "../bus.h"
#include "../irq.h"
#include <memory>
#include <string>

// Something attached to one RS-422 channel of an interface board: a virtual VTR, a switcher, ...
// The board hands the peer each host message and returns the peer's reply to the host.
// Host messages use the IDC framing [type, len, payload(len bytes), checksum] (see Rs422Board).
class SerialPeer {
public:
    virtual ~SerialPeer() {}
    virtual const char* kind() const = 0;
    // One host message (IDC framing, checksum already verified). Return the reply in the same framing
    // (the board adds the checksum), or an empty vector for "no answer" (host sees a receive time-out).
    virtual std::vector<uint8_t> request(const std::vector<uint8_t>& msg) = 0;
    // Advance device time (emulated seconds since the last call).
    virtual void advance(double seconds) {}
    // State for the HTTP /devices endpoint (a JSON object).
    virtual std::string status_json() const { return "{}"; }
    // Control from outside the editor (HTTP POST /devices/<name>): e.g. "play", "stop", "load".
    virtual bool control(const std::string& cmd, const std::string& arg) { return false; }
    std::string label;   // user-visible name, e.g. "VTR-A"
};

// RS-422 interface board with an IDC ("intelligent device controller") sub-CPU: SIO-3 (BKE-9002),
// IF-263 (BKE-9011), IF-328 (BKE-9012/9014), IF-342. Option slot n is at 0x0FFF9000 + n*0x1000;
// the 4 KB window is dual-port RAM shared with the board CPU, 1 KB per channel. Layout as used by the
// SRMS kernel (supsio3, ptcmdidc, ptprdcmd, intrd/intred) and the DIAG board test:
//   ch*0x400 + 0x000  RX: word byte count, then the reply message
//            + 0x200  TX: one host message [type, len, payload..., checksum]
//            + 0x300 + k*0x20  periodic command slot k (1..3), repeated by the board
//            + 0x380  (w) host command: 3 = reset channel, 2 = acknowledge error;  +0x381 bit0 = busy
//            + 0x382  (w) line format (kernel 0x0A77, DIAG 0x0F77)
//            + 0x384  (w) vector for "reply ready" (kernel: 0x60 + 2*port)
//            + 0x386  (w) vector for "error / time-out" (0x61 + 2*port)
//            + 0x390 + 2k (w) period of slot k,  + 0x398 + 2k (w) byte count of slot k (k = 0: one-shot TX)
//            + 0x3A0  (w) channel status: bit15 present, bits 8-9 ready; +0x3A1 = error code after a failure
//            + 0x3B8  (w) RX length (also read by the kernel)
//   board    + 0x3C0  (w) start (1) / reset (3);  + 0x3FC (w) firmware version;  + 0x3FE (w) board ID:
//            (ID & 0xF000) == 0x2000 marks an RS-422 board, bits 0-3 = channels fitted.
class Rs422Board : public Device {
public:
    Rs422Board(int slot, uint16_t id, IrqController* irq, int level);
    const char* name() const override { return name_.c_str(); }
    uint8_t read8(uint32_t off) override;
    void write8(uint32_t off, uint8_t v) override;
    void tick(uint32_t cycles) override;
    void reset() override;
    void attach(int ch, std::shared_ptr<SerialPeer> peer) { if (ch >= 0 && ch < 4) peers_[ch] = std::move(peer); }
    SerialPeer* peer(int ch) const { return (ch >= 0 && ch < 4) ? peers_[ch].get() : nullptr; }
    int slot() const { return slot_; }
    uint64_t messages = 0;   // host messages handled (for status)
private:
    uint16_t rd16(uint32_t o) const { return (uint16_t)((mem_[o] << 8) | mem_[o + 1]); }
    void wr16(uint32_t o, uint16_t v) { mem_[o] = (uint8_t)(v >> 8); mem_[o + 1] = (uint8_t)v; }
    void host_tx(int ch, int k);
    void service_periodic(int ch, int k);
    void raise(uint8_t vec);
    struct Pending { int ch; int64_t at; std::vector<uint8_t> reply; bool error; };
    std::string name_;
    int slot_; uint16_t id_;
    IrqController* irq_; int irq_id_;
    std::vector<uint8_t> mem_;
    std::shared_ptr<SerialPeer> peers_[4];
    std::vector<Pending> pending_;
    std::vector<uint8_t> vecq_;
    int64_t now_ = 0;
    int64_t periodic_next_[4][4] = {};
};
