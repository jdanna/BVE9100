#pragma once
#include "rs422.h"
#include <cstdlib>

// Replies start with the result code the host dispatches on: 0x04 = command completed, 0x02 = data message
// (also the format of periodic-slot replies).
// Behaviour common to every IDC (the sub-CPU on an RS-422 board): board-internal messages (type 0x02,
// command 0x09 = status/version) are answered here; device commands go to the subclass.
// Replies echo the command byte with 0x10 added (09 00 -> 19 00, 61 20 -> 71 20).
class IdcDevice : public SerialPeer {
public:
    explicit IdcDevice(uint16_t type) : type_(type) {}
    std::vector<uint8_t> request(const std::vector<uint8_t>& msg) override;
    uint16_t idc_type() const { return type_; }
protected:
    // msg = [type, len, cmd, sub, data...]; return the reply without checksum, or {} for no answer
    virtual std::vector<uint8_t> device_request(const std::vector<uint8_t>& msg) { return {}; }
    uint16_t type_;
};

// Experimental probe: answers only the status poll with a configurable type word (BVE_IDC_TYPE=hex)
class IdcProbe : public IdcDevice {
public:
    IdcProbe() : IdcDevice((uint16_t)strtoul(getenv("BVE_IDC_TYPE") ? getenv("BVE_IDC_TYPE") : "1001", nullptr, 16)) {}
    const char* kind() const override { return "probe"; }
};

// Virtual VTR behind a VTR IDC. Transport model with Sony-style status bits and a timecode counter;
// the IDC message set is being reverse-engineered (commands mirror Sony 9-pin: 61 20 = status sense,
// replies add 0x10 to the command byte).
class VirtualVtr : public IdcDevice {
public:
    enum Mode { STOP, PLAY, FF, REW, STILL, JOG, SHUTTLE, VAR, REC, EJECTED };
    explicit VirtualVtr(uint16_t type = 0x3001);
    const char* kind() const override { return "vtr"; }
    void advance(double seconds) override;
    std::string status_json() const override;
    bool control(const std::string& cmd, const std::string& arg) override;
    // timecode as frames (30 fps drop-free counting for display; NTSC)
    int64_t frames() const { return (int64_t)pos_; }
    std::string tc_string() const;
protected:
    std::vector<uint8_t> device_request(const std::vector<uint8_t>& msg) override;
    std::vector<uint8_t> status_bytes() const;
    Mode mode_ = STOP;
    double pos_ = 30 * 3600;     // tape position in frames (starts at 01:00:00:00)
    double speed_ = 0;           // frames per frame (1.0 = play)
    bool tape_ = true, standby_ = true, servo_ = false;
    double fps_ = 30000.0 / 1001.0;
    std::vector<uint8_t> last_cmd_;
    void cue_to(double frame);
    bool cueing_ = false; double cue_target_ = 0;
    bool cued_ = false; int cued_code_ = 0x10;      // servo-block code while parked at a cue point (ready)
    bool rolled_ = false, edit_done_ = false, rec_armed_ = false, synced_ = false;
    struct Event { uint32_t frame; uint8_t action; bool done = false; };
    std::vector<Event> events_;
    uint32_t edit_in_ = 0, rec_in_ = 0, rec_out_ = 0, rec_end_ = 0, preroll_ = 150;
    // Edit timeline: counted in the recorder's timecode. A deck at pos_ stands at pos_ - edit_in_ + rec_in_ on it
    // (each deck gets its own in point with 40 05 and the recorder's with 40 01), so decks parked one pre-roll
    // before their in points all sit at the same timeline position and roll together.
    double timeline_ = 0;
    double timeline_pos() const { return pos_ - (double)edit_in_ + (double)rec_in_; }
};

// Virtual video switcher behind a SW'ER IDC (IDC type 0x1xxx). Keeps crosspoint / transition / memory state
// for the editor; the message set is being mapped from the traffic the application sends (see docs).
class VirtualSwitcher : public IdcDevice {
public:
    explicit VirtualSwitcher(uint16_t type = 0x1001) : IdcDevice(type) {}
    const char* kind() const override { return "switcher"; }
    std::string status_json() const override;
    bool control(const std::string& cmd, const std::string& arg) override;
protected:
    std::vector<uint8_t> device_request(const std::vector<uint8_t>& msg) override;
    int pgm_ = 1, pst_ = 2;            // program / preset bus crosspoints
    int trans_ = 0;                    // 0 cut, 1 mix, 2 wipe
    int rate_ = 30;                    // transition rate in frames
    int wipe_ = 1;
    int snapshot_ = 0;                 // last recalled switcher memory (SWMEM)
    std::vector<std::vector<uint8_t>> log_;   // last commands seen (for /devices)
};
