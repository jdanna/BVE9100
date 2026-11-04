#pragma once
#include "common.h"

// Interrupt arbiter: maps named interrupt sources onto the 68030 IPL0-2 lines and
// supplies the interrupt vector during the IACK cycle.
// Sources are either "vectored" (a device supplies its own vector number, e.g. the 8259s
// and the DSC-63/SIO mailbox processors) or autovectored (vector 24+level).
class IrqController {
public:
    struct Line {
        std::string name;
        int level;                      // 68k IPL level 1..7
        bool active = false;
        std::function<int()> ack;       // returns vector number; may clear the line. nullptr = autovector
    };
    int add(const std::string& name, int level, std::function<int()> ack = nullptr) {
        lines_.push_back({name, level, false, std::move(ack)});
        return (int)lines_.size() - 1;
    }
    void set(int id, bool active) {
        if (lines_[id].active == active) return;
        lines_[id].active = active;
        LOGT("IRQ: %s %s (level %d)\n", lines_[id].name.c_str(), active ? "asserted" : "released", lines_[id].level);
        update();
    }
    int highest_level() const {
        int l = 0;
        for (auto& x : lines_) if (x.active && x.level > l) l = x.level;
        return l;
    }
    // IACK for a level: pick the first active line at that level.
    int acknowledge(int level) {
        for (auto& x : lines_) {
            if (x.active && x.level == level) {
                int vec = x.ack ? x.ack() : (24 + level);
                LOGT("IRQ: ack level %d -> %s vector %02x\n", level, x.name.c_str(), vec);
                update();
                return vec;
            }
        }
        LOGI("IRQ: spurious ack at level %d\n", level);
        return 24; // spurious interrupt vector
    }
    std::function<void(int level)> on_change;   // notify CPU of new IPL
    void update() { if (on_change) on_change(highest_level()); }
    const Line& line(int id) const { return lines_[id]; }
private:
    std::vector<Line> lines_;
};
