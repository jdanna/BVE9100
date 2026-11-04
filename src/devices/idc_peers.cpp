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
    cue_target_ = frame; cueing_ = true; synced_ = false; edit_done_ = false;
    mode_ = frame >= pos_ ? FF : REW;
    speed_ = frame >= pos_ ? 50 : -50;
}

void VirtualVtr::advance(double seconds) {
    if (cued_ && synced_) {                              // waiting for the start time on the edit timeline
        timeline_ += seconds * fps_;
        if (timeline_ >= timeline_pos()) {
            LOGI("IDC %s (vtr): roll from %s\n", label.c_str(), tc_string().c_str());
            cued_ = false; synced_ = false; mode_ = PLAY; speed_ = 1; rolled_ = true;
        }
        return;
    }
    if (rolled_ && speed_ > 0) {                         // timed events of the edit sequence (recorder in/out)
        double next = pos_ + speed_ * seconds * fps_;
        for (auto& e : events_) {
            if (e.done || (double)e.frame > next) continue;
            e.done = true;
            if (e.action == 0x12 && rec_armed_) { mode_ = REC; rec_start(e.frame, false); LOGI("IDC %s (vtr): record on at %s\n", label.c_str(), tc_string().c_str()); }
            else if (e.action == 0x11 && mode_ == REC) { mode_ = PLAY; rec_stop(e.frame); LOGI("IDC %s (vtr): record off at %s\n", label.c_str(), tc_string().c_str()); }
        }
    }
    if (rolled_ && rec_end_ > rec_in_) {                 // safety: stop 10 s after the end of the post-roll if 23 01 never comes
        double stop_at = (double)edit_in_ + (double)(rec_end_ - rec_in_) + 300;
        if (pos_ + speed_ * seconds * fps_ >= stop_at) {
            pos_ = stop_at; rolled_ = false; edit_done_ = false; mode_ = STOP; speed_ = 0;
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
        //   bit1 status:   [0xC0 = IDC talking to the deck + deck in REMOTE (0x80 alone -> LOCL/TOUT), +0x10 recording,
        //                  transport code (below)]
        //   bit2 search speed (tenths, bit 15 reverse, FFFF none), bit3 [servo group, play code 0x12, lock 0x20, extra], bit4 1 byte (unknown)
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
            // transport code (MAIN's reel-line text): 04 STOP (05 standby off), 08 PLAY, 0C FF, 0D REW,
            // 10 forward var, 11 reverse var, 12 STILL, 14 JOG, 18 / 19 / 1A shuttle forward / reverse / still
            uint8_t t = 0x04;
            switch (mode_) {
                case PLAY: case REC: t = 0x08; break;
                case VAR: t = speed_ < 0 ? 0x11 : 0x10; break;
                case JOG: t = 0x14; break;
                case SHUTTLE: t = speed_ > 0 ? 0x18 : speed_ < 0 ? 0x19 : 0x1A; break;
                case STILL: t = 0x12; break;
                case FF: t = 0x0C; break;
                case REW: t = 0x0D; break;
                default: t = 0x04; break;
            }
            r.push_back((uint8_t)((tape_ ? 0xC0 : 0x80) | (mode_ == REC ? 0x10 : 0))); r.push_back(t);   // 0x10 = recording
        }
        if (mask & 0x04) {
            // search speed for the shuttle display: tenths of play speed, bit 15 = reverse; FFFF = none
            uint16_t v = 0xFFFF;
            if (mode_ == SHUTTLE) { int t10 = (int)std::lround(std::fabs(speed_) * 10); v = (uint16_t)(std::min(t10, 0x7FFF) | (speed_ < 0 ? 0x8000 : 0)); }
            r.push_back((uint8_t)(v >> 8)); r.push_back((uint8_t)v);
        }
        if (mask & 0x08) {
            // IDC edit-sequencer phase in the low nibble of the first byte (AUTOX waits on it): 2 = cueing (3rd byte
            // 08) or parked (10); 3 + code 14 = parked and locked to the start timeline sent with 40 00; 4 + code 14 =
            // rolling in sync and, after 23 01, sequence ended; 0 = idle; bit 3 = error.
            if (rolled_) r.insert(r.end(), {0x04, 0x14, 0x31, 0x00});                                        // rolling in sync on the edit timeline
            else if (edit_done_) r.insert(r.end(), {0x04, 0x14, 0x10, 0x00});                              // sequence ended (after 23 01)
            else if (cueing_) r.insert(r.end(), {0x02, 0x00, 0x08, 0x00});                                 // cue in progress (MAIN: CUE> / CUE<)
            else if (cued_ && synced_) r.insert(r.end(), {0x03, 0x14, 0x10, 0x00});                        // parked and locked to the start timeline
            else if (cued_) r.insert(r.end(), {0x02, 0x00, (uint8_t)cued_code_, 0x00});                   // parked at the cue point
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
    if (cmd == 0x23) {
        // Edit-sequence end: 23 01 at the end of the post-roll (deck stops, reports phase 4 / code 14 "sequence
        // ended"), then 23 00 releases the deck (idle). 23 00 alone (ALL STOP, failed wait) aborts the sequence.
        if (sub == 0x01 && (rolled_ || cued_ || synced_)) {
            LOGI("IDC %s (vtr): edit sequence end at %s\n", label.c_str(), tc_string().c_str());
            rolled_ = cued_ = cueing_ = synced_ = false; edit_done_ = true;
            if (mode_ == REC) { mode_ = PLAY; rec_stop(pos_); }
            mode_ = STILL; speed_ = 0;
        } else if (sub == 0x00) {
            if (rolled_ || cued_ || cueing_ || synced_) {
                LOGI("IDC %s (vtr): edit sequence aborted at %s\n", label.c_str(), tc_string().c_str());
                if (mode_ == REC) rec_stop(pos_);
                mode_ = STILL; speed_ = 0;
            }
            rolled_ = cued_ = cueing_ = synced_ = edit_done_ = false;
        }
        last_cmd_ = m;
        return {0x04, 0, 0x33, sub};
    }
    if (cmd == 0x60) {
        // Edit-sequencer senses; data replies 70 xx with the payload sizes from the host's template table
        // (CMNDB 0xD002E3A6): 70 00: 4, 70 01: 9, 70 02: 5, 70 03: 4, 70 04: 7, 70 10: 4, 70 20: 12 bytes.
        // AUTOX sends 60 03 to every parked deck before the start and stores the 4 bytes: the deck's position on
        // the edit timeline (recorder timecode), from which it works out the start time it sends with 40 00.
        // Answering the raw tape position breaks edits whose source and recorder in points differ: AUTOX then
        // counts the timeline in the player's timecode and expects the recorder to roll that much later.
        static const struct { uint8_t sub, n; } sizes[] = {{0x00,4},{0x01,9},{0x02,5},{0x03,4},{0x04,7},{0x10,4},{0x20,12}};
        std::vector<uint8_t> r = {0x02, 0, 0x70, sub};
        int n = 0; for (auto& z : sizes) if (z.sub == sub) n = z.n;
        uint32_t fc = (uint32_t)(int64_t)(sub == 0x03 ? timeline_pos() : pos_);
        if (n >= 4) r.insert(r.end(), {(uint8_t)(fc >> 24), (uint8_t)(fc >> 16), (uint8_t)(fc >> 8), (uint8_t)fc});
        while ((int)r.size() < 4 + n) r.push_back(0);
        last_cmd_ = m;
        return r;
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
        preset_ = m[4];
        LOGI("IDC %s (vtr): edit preset %02x\n", label.c_str(), m[4]);
        events_.clear();
        last_cmd_ = m;
        return {0x04, 0, 0x52, sub};
    }
    if (cmd == 0x40 && m.size() >= 8) {                  // edit set-up from PREVIEW / AUTO EDIT: points as frame counts
        uint32_t v = ((uint32_t)m[4] << 24) | ((uint32_t)m[5] << 16) | ((uint32_t)m[6] << 8) | m[7];
        if (sub == 0x00 && cued_) {
            // Start timeline: where the edit timeline stands now (a few frames before the parked decks). It
            // advances one frame per frame; the deck rolls when it reaches the deck's parked timeline position.
            synced_ = true; timeline_ = (double)v;
            LOGI("IDC %s (vtr): start timeline %u, rolls in %.0f frames\n", label.c_str(), v, timeline_pos() - timeline_);
        }
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
            case 0x10: case 0x11: case 0x12: case 0x20: {
                // search: 10 JOG (enter), 20 jog step of N frames, 11 variable speed (DMC / SLOW), 12 shuttle;
                // data = [direction 0x80 = reverse, magnitude]. Speeds use the Sony 9-pin curve
                // 10^(N/32 - 2) x play (N = 64 -> 1x), limited to 50x.
                bool rev = m.size() > 4 && (m[4] & 0x80);
                int n = m.size() > 5 ? m[5] : 0;
                cueing_ = cued_ = false;
                if (sub == 0x20) { mode_ = JOG; speed_ = 0; pos_ += rev ? -n : n; if (pos_ < 0) pos_ = 0; break; }
                double v = n ? std::min(50.0, std::pow(10.0, n / 32.0 - 2.0)) : 0.0;
                mode_ = sub == 0x10 ? JOG : sub == 0x11 ? VAR : SHUTTLE;
                speed_ = rev ? -v : v;
                break;
            }
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
    std::string tn;
    static const char* tnames[] = {"V", "A1", "A2", "A3", "A4"};
    for (int i = 0; i < 5; i++) if (rec_tracks_ & (0x04 << i)) { if (!tn.empty()) tn += " "; tn += tnames[i]; }
    char b[640];
    // frame: tape position in frames (30 per timecode second, played at 29.97 per second), with the fraction
    // edit: the edit sequence this deck is in (armed = recorder armed by the edit preset, active = cueing / parked /
    // rolling, in / out = recorder in and out points); rec: the last recording (from / to in tape frames, to = -1 while
    // recording; tracks: V 0x04, A1-A4 0x08-0x40, also spelt out in "track_names")
    snprintf(b, sizeof b, "{\"kind\":\"vtr\",\"label\":\"%s\",\"mode\":\"%s\",\"tc\":\"%s\",\"frame\":%.2f,\"speed\":%.3f,\"tape\":%s,"
             "\"edit\":{\"armed\":%s,\"active\":%s,\"preset\":%u,\"in\":%u,\"out\":%u},"
             "\"rec\":{\"n\":%llu,\"from\":%.0f,\"to\":%.0f,\"tracks\":%u,\"track_names\":\"%s\",\"manual\":%s}}",
             label.c_str(), names[mode_], tc_string().c_str(), pos_, speed_, tape_ ? "true" : "false",
             rec_armed_ ? "true" : "false", (rolled_ || cued_ || cueing_ || synced_) ? "true" : "false", preset_, rec_in_, rec_out_,
             (unsigned long long)rec_n_, rec_from_, rec_to_, rec_tracks_, tn.c_str(), rec_manual_ ? "true" : "false");
    return b;
}

bool VirtualVtr::control(const std::string& cmd, const std::string& arg) {
    if (mode_ == REC && cmd != "rec") rec_stop(pos_);
    else if (mode_ != REC && cmd == "rec") rec_start(pos_, true);
    cueing_ = false;
    if (cmd != "stop" && cmd != "still") cued_ = false;
    rolled_ = false; edit_done_ = false; synced_ = false;
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

static uint32_t be32(const std::vector<uint8_t>& m, size_t i) {
    return i + 4 <= m.size() ? ((uint32_t)m[i] << 24) | ((uint32_t)m[i + 1] << 16) | ((uint32_t)m[i + 2] << 8) | m[i + 3] : 0;
}

void VirtualSwitcher::take(int to, int type, int frames, const char* why) {
    take_++; take_from_ = pgm_; take_to_ = to; take_type_ = type; take_frames_ = type ? frames : 0;
    if (!type || frames <= 0) { if (to != pgm_) pst_ = pgm_; pgm_ = to; in_trans_ = false; }
    else { in_trans_ = true; trans_to_ = to; pst_ = to; trans_pos_ = 0; trans_len_ = frames; }
    LOGI("IDC %s (switcher): %s %s to %d (%d frames, %s)\n", label.c_str(), why, type == 0 ? "cut" : type == 8 ? "wipe" : "mix",
         to, take_frames_, running_ ? "edit timeline" : "live");
}

void VirtualSwitcher::advance(double seconds) {
    double frames = seconds * fps_;
    if (in_trans_) {
        trans_pos_ += frames;
        if (trans_pos_ >= trans_len_) { in_trans_ = false; pst_ = pgm_; pgm_ = trans_to_; }
    }
    if (!running_) return;
    timeline_ += frames;
    // events due now, in frame order (the list arrives in order; several can share a frame)
    for (auto& e : events_) {
        if (e.done || (double)e.frame > timeline_) continue;
        e.done = true;
        const auto& d = e.data;
        char b[64]; snprintf(b, sizeof b, "%02x@%u", e.kind, e.frame); last_event_ = b;
        if (e.kind == 0x40 && d.size() >= 5) {
            // crosspoint: [type, ff, A, ff, B, ...]: type 0 = cut to A; 4 mix / 8 wipe = A on the program bus,
            // B on the preset bus, taken by the transition event that follows
            int type = d[0], A = d[2], B = d[4];
            if (type == 0) { if (A != 0xFF && A != pgm_) take(A, 0, 0, "event"); }
            else { trans_ = type; if (A != 0xFF && A != pgm_) take(A, 0, 0, "event"); if (B != 0xFF) pst_ = B; }
        } else if (e.kind == 0x50 && d.size() >= 3) {
            // transition: [type?, rate (fields, 16 bit)]: take the preset bus with the pending transition type
            rate_ = ((d[1] << 8) | d[2]) / 2;
            take(pst_, trans_ ? trans_ : 4, rate_, "event");
        } else if (e.kind == 0x60 && d.size() >= 2) {
            pattern_ = (d[0] << 8) | d[1];
        } else {
            LOGI("IDC %s (switcher): timed event %02x at %u not modelled\n", label.c_str(), e.kind, e.frame);
        }
    }
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
    if (cmd == 0x21 && sub == 0x40 && m.size() >= 9) {
        // live crosspoint on the program bus (MONITOR SELECT = PGM: source keys, end of a preview): 21 40 00 02 00 ff xx
        int x = m[8];
        if (x != 0xFF && x != pgm_) take(x, 0, 0, "source key");
        return {0x04, 0, 0x31, sub};
    }
    if (cmd == 0x41) {
        // timed events on the edit timeline: 41 kk fl 02 <frame (4)> <data>; 41 00 = clear the list
        if (sub == 0x00) { events_.clear(); running_ = false; }
        else if (m.size() >= 10) {
            Event e; e.frame = be32(m, 6); e.kind = sub; e.data.assign(m.begin() + 10, m.end());
            events_.push_back(e);
        }
        return {0x04, 0, 0x51, sub};
    }
    if (cmd == 0x40 && sub == 0x00 && m.size() >= 8) {   // start the edit timeline (same value as the VTRs get)
        timeline_ = (double)be32(m, 4); running_ = true;
        LOGI("IDC %s (switcher): timeline started at %u, %zu events\n", label.c_str(), be32(m, 4), events_.size());
        return {0x04, 0, 0x50, sub};
    }
    if (cmd == 0x24 && sub == 0x18) running_ = false;   // 24 10 at the roll, 24 18 at the end of the sequence
    if (g_verbose >= 1 && !(cmd == 0x24 || cmd == 0x42 || cmd == 0x4F)) {
        std::string h; char b[4]; for (uint8_t x : m) { snprintf(b, sizeof b, "%02x ", x); h += b; }
        LOGI("IDC %s (switcher): acked %s\n", label.c_str(), h.c_str());
    }
    return {0x04, 0, (uint8_t)(cmd + 0x10), sub};
}

std::string VirtualSwitcher::status_json() const {
    std::string last;
    for (auto& m : log_) {
        last += last.empty() ? "\"" : ",\"";
        char b[4]; for (uint8_t x : m) { snprintf(b, sizeof b, "%02x", x); last += b; }
        last += "\"";
    }
    int pending = 0; for (auto& e : events_) if (!e.done) pending++;
    const char* tn = trans_ == 8 ? "wipe" : trans_ == 4 ? "mix" : "cut";
    const char* tk = take_type_ == 8 ? "wipe" : take_type_ ? "mix" : "cut";
    char buf[768];
    snprintf(buf, sizeof buf,
             "{\"kind\":\"switcher\",\"label\":\"%s\",\"pgm\":%d,\"pst\":%d,\"transition\":\"%s\",\"rate\":%d,\"pattern\":%d,"
             "\"in_transition\":%s,\"progress\":%.3f,\"take\":{\"n\":%llu,\"type\":\"%s\",\"from\":%d,\"to\":%d,\"frames\":%d},"
             "\"timeline\":{\"running\":%s,\"frame\":%.0f,\"pending\":%d,\"last\":\"%s\"},\"snapshot\":%d,\"recent\":[",
             label.c_str(), pgm_, pst_, tn, rate_, pattern_, in_trans_ ? "true" : "false", in_trans_ && trans_len_ > 0 ? trans_pos_ / trans_len_ : 0.0,
             (unsigned long long)take_, tk, take_from_, take_to_, take_frames_, running_ ? "true" : "false", timeline_, pending,
             last_event_.c_str(), snapshot_);
    return std::string(buf) + last + "]}";
}

// From outside the editor (HTTP POST /devices/<port>): pgm N (cut), pst N, cut (take the preset bus),
// auto [frames] (transition to the preset bus), trans cut|mix|wipe, rate N (frames)
bool VirtualSwitcher::control(const std::string& cmd, const std::string& arg) {
    if (cmd == "pgm") take(atoi(arg.c_str()), 0, 0, "remote");
    else if (cmd == "pst") pst_ = atoi(arg.c_str());
    else if (cmd == "cut") take(pst_, 0, 0, "remote");
    else if (cmd == "auto") take(pst_, trans_ ? trans_ : 4, arg.empty() ? rate_ : atoi(arg.c_str()), "remote");
    else if (cmd == "trans") trans_ = arg == "wipe" ? 8 : arg == "mix" ? 4 : 0;
    else if (cmd == "rate") rate_ = std::max(1, atoi(arg.c_str()));
    else return false;
    return true;
}
