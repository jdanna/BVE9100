#include "idc_peers.h"
#include <algorithm>
#include <cmath>
#include <cstring>

std::vector<uint8_t> IdcDevice::request(const std::vector<uint8_t>& msg) {
    if (msg.size() < 3) return {};
    uint8_t cmd = msg[2], sub = msg.size() > 3 ? msg[3] : 0;
    if (msg[0] == 0x02 && cmd == 0x01) return {0x04, 0, 0x11, sub};                                  // IDC mode set: completed
    if (msg[0] == 0x02 && cmd == 0x09) {
        if (sub == 0x00) return {0x02, 0, 0x19, 0x00, (uint8_t)(type_ >> 8), (uint8_t)type_};      // device status/type
        if (sub == 0x01) return {0x02, 0, 0x19, 0x01, 0x01, 0x00};                                  // IDC version
        if (sub == 0x02) return {0x02, 0, 0x19, 0x02, 0x00, 0x00, 0x00, 0x00};
    }
    std::vector<uint8_t> r = device_request(msg);
    if (r.empty()) {
        std::string h; char b[4]; for (uint8_t x : msg) { snprintf(b, sizeof b, "%02x ", x); h += b; }
        LOGI("IDC %s (%s): unhandled %s\n", label.c_str(), kind(), h.c_str());
    }
    return r;
}

VirtualVtr::VirtualVtr(uint16_t type) : IdcDevice(type) {}

std::string VirtualVtr::tc_string() const {
    int64_t f = (int64_t)pos_; if (f < 0) f = 0;
    char b[16]; snprintf(b, sizeof b, "%02d:%02d:%02d:%02d", (int)(f / 108000) % 24, (int)(f / 1800) % 60, (int)(f / 30) % 60, (int)(f % 30));
    return b;
}

// Cue: wind at up to 50x toward the target, then park in STILL (as a Sony deck does after CUE UP)
void VirtualVtr::cue_to(double frame) {
    cue_target_ = frame; cueing_ = true;
    mode_ = frame >= pos_ ? FF : REW;
    speed_ = frame >= pos_ ? 50 : -50;
}

void VirtualVtr::advance(double seconds) {
    if (rolled_ && speed_ > 0) {                         // timed events of the edit sequence (recorder in/out)
        double next = pos_ + speed_ * seconds * fps_;
        for (auto& e : events_) {
            if (e.done || (double)e.frame > next) continue;
            e.done = true;
            if (e.action == 0x12 && rec_armed_) { mode_ = REC; LOGI("IDC %s (vtr): record on at %s\n", label.c_str(), tc_string().c_str()); }
            else if (e.action == 0x11 && mode_ == REC) { mode_ = PLAY; LOGI("IDC %s (vtr): record off at %s\n", label.c_str(), tc_string().c_str()); }
        }
    }
    if (rolled_ && rec_end_ > rec_in_) {                 // edit roll: stop at this deck's equivalent of recorder out + post-roll
        double stop_at = (double)edit_in_ + (double)(rec_end_ - rec_in_);
        if (pos_ + speed_ * seconds * fps_ >= stop_at) {
            pos_ = stop_at; rolled_ = false; edit_done_ = true; mode_ = STOP; speed_ = 0;
            LOGI("IDC %s (vtr): edit roll finished at %s\n", label.c_str(), tc_string().c_str());
            return;
        }
    }
    if (cueing_) {
        double step = 50 * seconds * fps_;
        if (std::fabs(cue_target_ - pos_) <= step) { pos_ = cue_target_; cueing_ = false; cued_ = true; mode_ = STILL; speed_ = 0; return; }
        pos_ += cue_target_ > pos_ ? step : -step;
        return;
    }
    pos_ += speed_ * seconds * fps_;
    if (pos_ < 0) { pos_ = 0; speed_ = 0; mode_ = STOP; }
    servo_ = (mode_ == PLAY || mode_ == REC);
}

static inline uint8_t bit(bool b, int n) { return b ? (uint8_t)(1 << n) : 0; }

// Sony 9-pin STATUS DATA layout (bytes 0..15)
std::vector<uint8_t> VirtualVtr::status_bytes() const {
    std::vector<uint8_t> s(16, 0);
    s[0] = bit(!tape_, 5);
    s[1] = bit(standby_, 7) | bit(mode_ == STOP, 5) | bit(mode_ == REW, 3) | bit(mode_ == FF, 2) | bit(mode_ == REC, 1) | bit(mode_ == PLAY || mode_ == REC, 0);
    s[2] = bit(servo_, 7) | bit(mode_ == SHUTTLE, 5) | bit(mode_ == JOG, 4) | bit(mode_ == VAR, 3) | bit(speed_ < 0, 2) | bit(mode_ == STILL, 1);
    return s;
}

std::vector<uint8_t> VirtualVtr::device_request(const std::vector<uint8_t>& m) {
    uint8_t cmd = m[2], sub = m.size() > 3 ? m[3] : 0;
    if (g_verbose >= 2 && cmd != 0x08 && cmd != 0x61) {
        std::string h; char b[4]; for (uint8_t x : m) { snprintf(b, sizeof b, "%02x ", x); h += b; }
        LOGD("IDC %s (vtr): <- %s(tc %s)\n", label.c_str(), h.c_str(), tc_string().c_str());
    }
    if (cmd == 0x08) {                                   // IDC poll 08 xx (device state)
        if (sub == 0x00) return {0x02, 0, 0x18, 0x00, 0x00, 0x00};
        return {0x02, 0, 0x18, sub, 0x00};
    }
    if (cmd == 0x61 && sub == 0x20) {
        // Periodic STATUS SENSE (installed by STRAPL in slot 2, every field): data byte = mask of blocks wanted.
        // Reply [02, len, 71, 20, mask, blocks...] parsed by STRAPL (FUN_c6022844) into a per-port 20-byte record
        // that MAIN draws on the reel line:
        //   bit0 timecode: type (bit7 = LTC, no '.' after the TC), TC = binary frame count (4 bytes, big-endian), user bits 4 bytes
        //   bit1 status:   [0xC0 = IDC talking to the deck + deck in REMOTE (0x80 alone -> LOCL/TOUT), transport code]
        //                  transport code: 04 STOP, 0C FF, 0D REW, 10 FWD/var, 11 REV, 12 STILL
        //   bit2 2 bytes (unknown), bit3 [servo group, play code 0x12, lock 0x20, extra], bit4 1 byte (unknown)
        uint8_t mask = m.size() > 4 ? m[4] : 0x1F;
        std::vector<uint8_t> r = {0x02, 0, 0x71, 0x20, mask};
        int64_t f = (int64_t)pos_; if (f < 0) f = 0;
        if (mask & 0x01) {
            r.push_back(0x80);
            uint32_t fc = (uint32_t)(f % (24 * 108000));     // timecode as a binary frame count, big-endian
            r.insert(r.end(), {(uint8_t)(fc >> 24), (uint8_t)(fc >> 16), (uint8_t)(fc >> 8), (uint8_t)fc});
            r.insert(r.end(), {0, 0, 0, 0});
        }
        if (mask & 0x02) {
            uint8_t t = 0x04;
            switch (mode_) {
                case PLAY: case REC: case VAR: case JOG: case SHUTTLE: t = speed_ < 0 ? 0x11 : 0x10; break;
                case STILL: t = 0x12; break;
                case FF: t = 0x0C; break;
                case REW: t = 0x0D; break;
                default: t = 0x04; break;
            }
            r.push_back((uint8_t)((tape_ ? 0xC0 : 0x80) | (mode_ == REC ? 0x10 : 0))); r.push_back(t);   // 0x10 = recording
        }
        if (mask & 0x04) { r.push_back(0); r.push_back(0); }
        if (mask & 0x08) {
            // IDC edit-sequencer phase in the low nibble of the first byte (AUTOX waits on it): 2 = active (cueing:
            // 3rd byte 08, parked: 10, rolling: code 12 + 31 LOCK), 4 = edit sequence finished, 0 = idle; bit 3 = error.
            if (mode_ == REC && rolled_) r.insert(r.end(), {0x02, 0x12, 0x00, 0x00});                      // recording: MAIN falls through to the REC display
            else if ((mode_ == PLAY || mode_ == REC) && rolled_) r.insert(r.end(), {0x02, 0x12, 0x31, 0x00});   // rolling for an edit (LOCK)
            else if (edit_done_) r.insert(r.end(), {0x04, 0x12, 0x10, 0x00});                              // edit sequence finished
            else if (mode_ == PLAY || mode_ == REC) r.insert(r.end(), {0x02, 0x12, 0x20, 0x00});
            else if (cueing_) r.insert(r.end(), {0x02, 0x00, 0x08, 0x00});     // cue in progress (MAIN: CUE> / CUE<)
            else if (cued_) r.insert(r.end(), {0x02, 0x00, (uint8_t)cued_code_, 0x00});   // parked at the cue point
            else r.insert(r.end(), {0x00, 0x00, 0x00, 0x00});
        }
        if (mask & 0x10) r.push_back(0);
        return r;
    }
    if (cmd == 0x61 && sub == 0x10) {
        // Device information (polled by STRAPL): data reply 71 10 + 24 bytes, copied into a per-port block
        // in CMNDB (0xD002DC6C + port*0x18). Byte 0 = Sony device type (0x20 0x25 = BVW-75 class, guess);
        // byte 22 bits 0/2/3 set per-port capability bitmaps (meaning unknown).
        std::vector<uint8_t> r = {0x02, 0, 0x71, 0x10, 0x20, 0x25};
        r.resize(4 + 24, 0);
        r[4 + 22] = 0x0D;
        return r;
    }
    if (cmd == 0x22 && sub == 0x05 && m.size() >= 8) {   // CUE UP to a frame count (GO TO, preview/edit pre-roll)
        double target = (double)(((uint32_t)m[4] << 24) | ((uint32_t)m[5] << 16) | ((uint32_t)m[6] << 8) | m[7]);
        cue_to(target);
        last_cmd_ = m;
        return {0x04, 0, 0x32, sub};
    }
    if (cmd == 0x23 && sub == 0x00) {
        // Edit-sequence control, sent to all decks of an edit at once: starts the synchronised roll of parked
        // decks; sent again to rolling decks by ALL STOP, where it aborts the sequence.
        if (cued_) {
            LOGI("IDC %s (vtr): roll from %s\n", label.c_str(), tc_string().c_str());
            cued_ = false; mode_ = PLAY; speed_ = 1; rolled_ = true;
        } else if (rolled_) {
            LOGI("IDC %s (vtr): edit sequence aborted at %s\n", label.c_str(), tc_string().c_str());
            rolled_ = false; edit_done_ = false; mode_ = STOP; speed_ = 0;
        }
        last_cmd_ = m;
        return {0x04, 0, 0x33, sub};
    }
    if (cmd == 0x60) {                                   // sense without data (60 03 after a cue); answered as done
        last_cmd_ = m;
        return {0x04, 0, 0x70, sub};
    }
    if (cmd == 0x41 && sub == 0x01 && m.size() >= 10) {   // timed event: at frame <4 bytes> do <action> (12 = record on, 11 = record off)
        uint32_t f = ((uint32_t)m[4] << 24) | ((uint32_t)m[5] << 16) | ((uint32_t)m[6] << 8) | m[7];
        events_.push_back({f, m[8]});
        LOGI("IDC %s (vtr): event at frame %u action %02x\n", label.c_str(), f, m[8]);
        last_cmd_ = m;
        return {0x04, 0, 0x51, sub};
    }
    if (cmd == 0x42 && sub == 0x06 && m.size() >= 5) {  // edit preset: bit 7 = recorder armed (auto edit), low bits = tracks
        rec_armed_ = (m[4] & 0x80) != 0;
        events_.clear();
        last_cmd_ = m;
        return {0x04, 0, 0x52, sub};
    }
    if (cmd == 0x40 && m.size() >= 8) {                  // edit set-up from PREVIEW / AUTO EDIT: points as frame counts
        uint32_t v = ((uint32_t)m[4] << 24) | ((uint32_t)m[5] << 16) | ((uint32_t)m[6] << 8) | m[7];
        if (sub == 0x05) edit_in_ = v;                   // this deck's in point (sent twice)
        else if (sub == 0x01) rec_in_ = v;               // recorder in point (sent twice)
        else if (sub == 0x02) rec_out_ = v;              // recorder out point
        else if (sub == 0x03) rec_end_ = v;              // recorder out + post-roll: where the decks stop
        else if (sub == 0x10) preroll_ = v;              // pre-roll in frames (150)
        last_cmd_ = m;
        return {0x04, 0, 0x50, sub};
    }
    if (cmd == 0x22 && sub == 0x14) {                    // cue to the pre-roll point of the edit set up with 40 xx
        double preroll = preroll_ ? preroll_ : 150;
        cue_to((double)edit_in_ - preroll);
        LOGI("IDC %s (vtr): cue to pre-roll point (in %u - %.0f)\n", label.c_str(), edit_in_, preroll);
        last_cmd_ = m;
        return {0x04, 0, 0x32, sub};
    }
    if (cmd == 0x20) {                                   // transport (IDC numbering, Sony 9-pin style)
        switch (sub) {
            case 0x00: control("stop", ""); break;
            case 0x01: control("play", ""); break;
            case 0x02: control("rec", ""); break;
            case 0x03: control("still", ""); break;
            case 0x06: control("ff", ""); break;
            case 0x07: control("rew", ""); break;
            case 0x10: mode_ = JOG; speed_ = 0; cueing_ = false; break;
            case 0x12: mode_ = SHUTTLE; speed_ = 0; cueing_ = false; break;
            default: LOGI("IDC %s (vtr): transport %02x not modelled\n", label.c_str(), sub); break;
        }
        last_cmd_ = m;
        return {0x04, 0, 0x30, sub};
    }
    // not understood yet: acknowledge with the reply command (cmd + 0x10) so the host carries on
    std::string h; char b[4]; for (uint8_t x : m) { snprintf(b, sizeof b, "%02x ", x); h += b; }
    LOGI("IDC %s (vtr): acked unknown %s\n", label.c_str(), h.c_str());
    last_cmd_ = m;
    return {0x04, 0, (uint8_t)(cmd + 0x10), sub};
}

std::string VirtualVtr::status_json() const {
    static const char* names[] = {"stop", "play", "ff", "rew", "still", "jog", "shuttle", "var", "rec", "ejected"};
    char b[256];
    snprintf(b, sizeof b, "{\"kind\":\"vtr\",\"label\":\"%s\",\"mode\":\"%s\",\"tc\":\"%s\",\"speed\":%.3f,\"tape\":%s}",
             label.c_str(), names[mode_], tc_string().c_str(), speed_, tape_ ? "true" : "false");
    return b;
}

bool VirtualVtr::control(const std::string& cmd, const std::string& arg) {
    cueing_ = false;
    if (cmd != "stop" && cmd != "still") cued_ = false;
    rolled_ = false; edit_done_ = false;
    for (auto& e : events_) e.done = true;
    if (cmd == "cue") { cue_to(atof(arg.c_str())); return true; }
    if (cmd == "play") { mode_ = PLAY; speed_ = 1; standby_ = true; }
    else if (cmd == "rec") { mode_ = REC; speed_ = 1; }
    else if (cmd == "still") { mode_ = STILL; speed_ = 0; }
    else if (cmd == "goto") { pos_ = atof(arg.c_str()); mode_ = STILL; speed_ = 0; }   // frame number
    else if (cmd == "stop") { mode_ = STOP; speed_ = 0; }
    else if (cmd == "ff") { mode_ = FF; speed_ = 10; }
    else if (cmd == "rew") { mode_ = REW; speed_ = -10; }
    else if (cmd == "eject") { mode_ = EJECTED; speed_ = 0; tape_ = false; }
    else if (cmd == "load") { mode_ = STOP; speed_ = 0; tape_ = true; }
    else return false;
    return true;
}

std::vector<uint8_t> VirtualSwitcher::device_request(const std::vector<uint8_t>& m) {
    uint8_t cmd = m[2], sub = m.size() > 3 ? m[3] : 0;
    if (!(cmd == 0x64 || cmd == 0x6C || cmd == 0x08)) { log_.push_back(m); if (log_.size() > 16) log_.erase(log_.begin()); }
    if (cmd == 0x08) {                                   // IDC poll 08 xx (device state)
        if (sub == 0x00) return {0x02, 0, 0x18, 0x00, 0x00, 0x00};
        return {0x02, 0, 0x18, sub, 0x00};
    }
    if ((cmd == 0x64 || cmd == 0x6C) && sub == 0x80) {
        // Switcher status (periodic slot 1, every 3 fields): 64 80 short form, 6C 80 00 <flags> extended form.
        // Reply parsed by STRAPL (FUN_c6023fc4): [02, len, 7C, 80, 00, flags, status word,
        //   flags bit0: 4 bytes, bit1: 2 bytes, bit7: 2 bytes]. Status word 0 = no fault.
        uint8_t flags = (cmd == 0x6C && m.size() > 5) ? m[5] : 0x00;
        std::vector<uint8_t> r = {0x02, 0, 0x7C, 0x80, 0x00, flags, 0x00, 0x00};
        if (flags & 0x01) r.insert(r.end(), {0, 0, 0, 0});
        if (flags & 0x02) r.insert(r.end(), {0, 0});
        if (flags & 0x80) r.insert(r.end(), {0, 0});
        return r;
    }
    std::string h; char b[4]; for (uint8_t x : m) { snprintf(b, sizeof b, "%02x ", x); h += b; }
    LOGI("IDC %s (switcher): acked %s\n", label.c_str(), h.c_str());
    return {0x04, 0, (uint8_t)(cmd + 0x10), sub};
}

std::string VirtualSwitcher::status_json() const {
    std::string last;
    for (auto& m : log_) {
        last += last.empty() ? "\"" : ",\"";
        char b[4]; for (uint8_t x : m) { snprintf(b, sizeof b, "%02x", x); last += b; }
        last += "\"";
    }
    char buf[256];
    snprintf(buf, sizeof buf, "{\"kind\":\"switcher\",\"label\":\"%s\",\"pgm\":%d,\"pst\":%d,\"transition\":%d,\"rate\":%d,\"snapshot\":%d,\"recent\":[",
             label.c_str(), pgm_, pst_, trans_, rate_, snapshot_);
    return std::string(buf) + last + "]}";
}

bool VirtualSwitcher::control(const std::string& cmd, const std::string& arg) {
    if (cmd == "pgm") pgm_ = atoi(arg.c_str());
    else if (cmd == "pst") pst_ = atoi(arg.c_str());
    else if (cmd == "cut") std::swap(pgm_, pst_);
    else return false;
    return true;
}
