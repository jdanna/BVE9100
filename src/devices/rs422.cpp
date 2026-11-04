#include "rs422.h"
#include <algorithm>
#include <set>
extern "C" unsigned int m68k_get_reg(void* context, int reg);
#define M68K_REG_PC_ID 16

// Board timing (assumed): the IDC answers a host message after ~1 ms (+ wire time), and reports a
// receive time-out after ~20 ms when nothing is attached.
static constexpr int64_t REPLY_DELAY = CPU_HZ / 1000;
static constexpr int64_t TIMEOUT_DELAY = CPU_HZ / 50;

static std::string hex(const std::vector<uint8_t>& v) {
    std::string s; char b[4];
    for (uint8_t x : v) { snprintf(b, sizeof b, "%02x ", x); s += b; }
    return s;
}

Rs422Board::Rs422Board(int slot, uint16_t id, IrqController* irq, int level)
    : slot_(slot), id_(id), irq_(irq), mem_(0x1000, 0) {
    name_ = "RS422." + std::to_string(slot);
    irq_id_ = irq->add(name_, level, [this]() {
        uint8_t v = vecq_.empty() ? 0x18 : vecq_.front();
        if (!vecq_.empty()) vecq_.erase(vecq_.begin());
        if (vecq_.empty()) irq_->set(irq_id_, false);
        return (int)v;
    });
    reset();
}

void Rs422Board::reset() {
    std::fill(mem_.begin(), mem_.end(), 0);
    pending_.clear(); vecq_.clear();
    for (auto& c : periodic_next_) for (auto& t : c) t = 0;
    wr16(0x3FE, id_);
    wr16(0x3FC, 0x1050);                          // firmware "V1.050" (any value; shown by DIAG)
    for (int ch = 0; ch < 4; ch++) if (id_ & (1 << ch)) wr16(ch * 0x400 + 0x3A0, 0x8300);   // present + ready
}

void Rs422Board::raise(uint8_t vec) { if (!vec) return; vecq_.push_back(vec); irq_->set(irq_id_, true); }

uint8_t Rs422Board::read8(uint32_t off) {
    if (off >= mem_.size()) return 0xFF;
    if (g_verbose >= 2) {                           // discovery aid: first read of each (offset, pc)
        static std::set<uint64_t> seen;
        uint32_t pc = m68k_get_reg(nullptr, M68K_REG_PC_ID);
        if (seen.insert(((uint64_t)pc << 16) | (off & 0x3FF)).second && seen.size() < 4000)
            LOGD("%s: read ch%u +%03x (pc %08x)\n", name_.c_str(), off / 0x400, off & 0x3FF, pc);
    }
    // keep the board ID visible even if the host scribbles over it
    if ((off & ~1u) == 0x3FE) return (off & 1) ? (uint8_t)id_ : (uint8_t)(id_ >> 8);
    return mem_[off];
}

void Rs422Board::write8(uint32_t off, uint8_t v) {
    if (off >= mem_.size()) return;
    mem_[off] = v;
    int ch = (int)(off / 0x400); uint32_t r = off & 0x3FF;
    if (r >= 0x380) LOGD("%s: ch%d reg %03x <- %02x (pc %08x)\n", name_.c_str(), ch, r, v, m68k_get_reg(nullptr, M68K_REG_PC_ID));
    if (!(off & 1)) return;                         // act on the low byte of word registers
    if (r == 0x381) {
        uint16_t cmd = rd16(ch * 0x400 + 0x380);
        if (cmd == 3) {                             // reset channel
            pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [ch](const Pending& p) { return p.ch == ch; }), pending_.end());
            mem_[ch * 0x400 + 0x3A1] = 0;
            LOGI("%s: ch%d reset by host\n", name_.c_str(), ch);
        } else if (cmd == 2) {
            mem_[ch * 0x400 + 0x3A1] = 0;           // error acknowledged
        }
        wr16(ch * 0x400 + 0x380, 0);
    } else if (r >= 0x399 && r <= 0x39F && ((r - 0x399) % 2) == 0) {
        int k = (int)(r - 0x399) / 2;
        if (rd16(ch * 0x400 + 0x398 + 2 * k)) host_tx(ch, k);
        else if (k) { periodic_next_[ch][k] = 0; wr16(ch * 0x400 + 0x3B0 + 2 * k, 0xFFFF); }
    } else if (r == 0x3C1 && ch == 0) {
        LOGI("%s: board command %04x\n", name_.c_str(), rd16(0x3C0));
        if (rd16(0x3C0) == 3) reset();
    }
}

// The host handed the board a message (k = 0: one-shot TX buffer at +0x200; k = 1..3: periodic slot).
void Rs422Board::host_tx(int ch, int k) {
    uint32_t b = ch * 0x400;
    uint16_t n = rd16(b + 0x398 + 2 * k);
    uint32_t src = k ? b + 0x300 + 0x20 * k : b + 0x200;
    uint32_t maxlen = k ? 0x20 : 0x100;
    if (n > maxlen) n = (uint16_t)maxlen;
    std::vector<uint8_t> msg(mem_.begin() + src, mem_.begin() + src + n);
    if (k) {                                       // periodic slot: remember it, the board repeats it
        uint16_t period = rd16(b + 0x390 + 2 * k);
        LOGD("%s: ch%d periodic slot %d period %04x: %s\n", name_.c_str(), ch, k, period, hex(msg).c_str());
        periodic_next_[ch][k] = now_ + REPLY_DELAY;
        return;
    }
    wr16(b + 0x398, 0);
    messages++;
    // The kernel copies len+3 bytes; the application's message constants carry no checksum (the extra byte
    // is whatever follows in memory), so only the length is checked.
    bool ok = msg.size() >= 2 && msg.size() >= (size_t)msg[1] + 2;
    SerialPeer* p = peers_[ch].get();
    LOGD("%s: ch%d host msg%s: %s%s\n", name_.c_str(), ch, ok ? "" : " (bad framing)", hex(msg).c_str(), p ? "" : " [nothing attached]");
    Pending pd{ch, now_ + TIMEOUT_DELAY, {}, true};
    if (p && ok) {
        std::vector<uint8_t> rep = p->request(std::vector<uint8_t>(msg.begin(), msg.begin() + msg[1] + 2));
        if (!rep.empty()) {
            if (rep.size() >= 2) rep[1] = (uint8_t)(rep.size() - 2);
            pd = Pending{ch, now_ + REPLY_DELAY, rep, false};
        }
    }
    pending_.push_back(pd);
}

// Periodic slot k: the board re-sends the message every <period hi byte> fields and leaves the latest
// reply in the channel's +0x120 area, publishing its offset (relative to +0x120) in +0x3B0 + 2k.
void Rs422Board::service_periodic(int ch, int k) {
    uint32_t b = ch * 0x400;
    uint16_t n = rd16(b + 0x398 + 2 * k);
    if (!n || !peers_[ch]) return;
    if (n > 0x20) n = 0x20;
    std::vector<uint8_t> msg(mem_.begin() + b + 0x300 + 0x20 * k, mem_.begin() + b + 0x300 + 0x20 * k + n);
    if (msg.size() < 2) return;
    msg.resize(std::min<size_t>(msg.size(), (size_t)msg[1] + 2));
    std::vector<uint8_t> rep = peers_[ch]->request(msg);
    if (rep.empty()) return;
    if (rep.size() >= 2) rep[1] = (uint8_t)(rep.size() - 2);
    uint8_t s = 0; for (size_t i = 2; i < rep.size(); i++) s += rep[i];
    rep.push_back((uint8_t)(0x100 - s));
    uint32_t off = 0x10 + 0x40 * (k - 1);           // slot 1 -> +0x130, 2 -> +0x170, 3 -> +0x1B0 (0 = "none")
    if (rep.size() > 0x40) rep.resize(0x40);
    std::copy(rep.begin(), rep.end(), mem_.begin() + b + 0x120 + off);
    wr16(b + 0x3B0 + 2 * k, (uint16_t)off);
    if (g_verbose >= 3) LOGT("%s: ch%d slot %d reply %s\n", name_.c_str(), ch, k, hex(rep).c_str());
}

void Rs422Board::tick(uint32_t cycles) {
    now_ += cycles;
    for (int ch = 0; ch < 4; ch++)
        for (int k = 1; k < 4; k++) {
            if (!periodic_next_[ch][k] || now_ < periodic_next_[ch][k]) continue;
            uint16_t period = rd16(ch * 0x400 + 0x390 + 2 * k);
            int fields = period >> 8; if (fields < 1) fields = 1;
            periodic_next_[ch][k] = now_ + (int64_t)fields * (CPU_HZ * 1001 / 60000);
            service_periodic(ch, k);
        }
    for (int ch = 0; ch < 4; ch++) if (peers_[ch]) peers_[ch]->advance((double)cycles / CPU_HZ);
    for (size_t i = 0; i < pending_.size();) {
        Pending& p = pending_[i];
        if (now_ < p.at) { i++; continue; }
        uint32_t b = p.ch * 0x400;
        if (p.error) {
            mem_[b + 0x3A1] = 0x02;                  // receive time-out (code assumed)
            raise(mem_[b + 0x387]);
            LOGD("%s: ch%d time-out -> vector %02x\n", name_.c_str(), p.ch, mem_[b + 0x387]);
        } else {
            // RX area: the reply message [code, len, ...]; the kernel posts its first +0x3B8 bytes to the
            // requesting task (i_semsg), which dispatches on the first byte: 4 = command completed,
            // 2 = data message for the device-class handler, 5 / 0xFF = error.
            std::copy(p.reply.begin(), p.reply.end(), mem_.begin() + b);
            wr16(b + 0x3B8, (uint16_t)p.reply.size());
            raise(mem_[b + 0x385]);
            LOGD("%s: ch%d reply %s-> vector %02x\n", name_.c_str(), p.ch, hex(p.reply).c_str(), mem_[b + 0x385]);
        }
        pending_.erase(pending_.begin() + i);
    }
}
