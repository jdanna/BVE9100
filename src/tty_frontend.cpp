#include "tty_frontend.h"
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <fstream>
#include <sstream>

static struct termios g_saved; static bool g_saved_ok = false;

bool TtyFrontend::start() {
    if (!isatty(0)) return false;
    if (tcgetattr(0, &g_saved) != 0) return false;
    g_saved_ok = true;
    struct termios t = g_saved;
    t.c_lflag &= ~(ICANON | ECHO | ISIG);
    t.c_iflag &= ~(IXON | ICRNL);
    t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &t);
    raw_ = true;
    fputs("\033[?25l", stdout);
    return true;
}
void TtyFrontend::stop() {
    if (g_saved_ok && raw_) tcsetattr(0, TCSANOW, &g_saved);
    raw_ = false;
    fputs("\033[?25h\033[0m\n", stdout); fflush(stdout);
}
// Defaults match keymap.txt (factory key assignment, see docs/keyboard-codes.md); override with --keymap FILE
// (lines: <name> <hex> [<hex> ...]).
// Names: single printable characters, or ENTER ESC TAB BS DEL UP DOWN LEFT RIGHT F1..F12 HOME END PGUP PGDN.
void TtyFrontend::set_default_keymap() {
    keymap.clear();
    keymap["ENTER"] = {0x0A}; keymap["HOME"] = {0x0D}; keymap["ESC"] = {0xE0};                      // ENTER, RET, INIT
    for (int i = 1; i <= 8; ++i) keymap["F" + std::to_string(i)] = {0x80, (uint8_t)(0x17 + i)};    // F1-F8
    keymap["F9"] = {0x80, 0x13}; keymap["F10"] = {0x80, 0x14}; keymap["F11"] = {0x80, 0x15}; keymap["F12"] = {0x80, 0x16}; // PF1-4
    keymap["UP"] = {0x80, 0x24}; keymap["LEFT"] = {0x80, 0x25}; keymap["DOWN"] = {0x80, 0x26}; keymap["RIGHT"] = {0x80, 0x27};
    keymap["PGUP"] = {0x80, 0x22}; keymap["PGDN"] = {0x80, 0x21}; keymap["DEL"] = {0x80, 0x23};  // YES, NO, CLR 1
}
bool TtyFrontend::load_keymap(const std::string& path, std::string* err) {
    std::ifstream f(path);
    if (!f) { if (err) *err = "cannot open keymap " + path; return false; }
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line); std::string name, hex; std::vector<uint8_t> bytes;
        ss >> name;
        while (ss >> hex) bytes.push_back((uint8_t)strtoul(hex.c_str(), nullptr, 16));
        if (!bytes.empty()) keymap[name] = bytes;
    }
    return true;
}
std::vector<uint8_t> TtyFrontend::poll_keys() {
    std::vector<uint8_t> out;
    fd_set fds; FD_ZERO(&fds); FD_SET(0, &fds);
    struct timeval tv = {0, 0};
    if (select(1, &fds, nullptr, nullptr, &tv) > 0) {
        char buf[64]; int n = (int)read(0, buf, sizeof buf);
        if (n > 0) pending_.append(buf, n);
    }
    while (!pending_.empty()) {
        std::string name;
        unsigned char c = pending_[0];
        size_t used = 1;
        if (c == 0x1D) { quit_requested = true; pending_.clear(); break; }        // Ctrl-]
        if (c == 0x1B) {
            if (pending_.size() == 1) break;                                        // wait for more
            // CSI / SS3 sequences
            static const struct { const char* seq; const char* name; } tbl[] = {
                {"[A", "UP"}, {"[B", "DOWN"}, {"[C", "RIGHT"}, {"[D", "LEFT"}, {"[H", "HOME"}, {"[F", "END"},
                {"OP", "F1"}, {"OQ", "F2"}, {"OR", "F3"}, {"OS", "F4"}, {"[15~", "F5"}, {"[17~", "F6"}, {"[18~", "F7"},
                {"[19~", "F8"}, {"[20~", "F9"}, {"[21~", "F10"}, {"[23~", "F11"}, {"[24~", "F12"}, {"[5~", "PGUP"}, {"[6~", "PGDN"},
                {"[3~", "DEL"}, {"[1~", "HOME"}, {"[4~", "END"}, {"[11~", "F1"}, {"[12~", "F2"}, {"[13~", "F3"}, {"[14~", "F4"}};
            bool matched = false, prefix = false;
            for (auto& e : tbl) {
                std::string s = std::string("\033") + e.seq;
                if (pending_.compare(0, s.size(), s) == 0) { name = e.name; used = s.size(); matched = true; break; }
                if (s.compare(0, pending_.size(), pending_) == 0) prefix = true;
            }
            if (!matched) { if (prefix) break; name = "ESC"; used = 1; }
        } else if (c == '\r' || c == '\n') name = "ENTER";
        else if (c == '\t') name = "TAB";
        else if (c == 0x7F || c == 0x08) name = "BS";
        else if (c >= 0x20 && c < 0x7F) name = std::string(1, (char)c);
        else name = "CTRL-" + std::string(1, (char)(c + '@'));
        pending_.erase(0, used);
        auto it = keymap.find(name);
        if (it != keymap.end()) out.insert(out.end(), it->second.begin(), it->second.end());
        else if (name.size() == 1) out.push_back((uint8_t)name[0]);     // BKE-9400A wire codes are the ASCII codes of the key legends
        else LOGI("TTY: unmapped host key '%s'\n", name.c_str());
    }
    return out;
}
