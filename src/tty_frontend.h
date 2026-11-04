#pragma once
#include "common.h"
#include <map>
// Terminal front-end: raw stdin -> keyboard bytes, ANSI redraw of the DSC-63 text screen.
class TtyFrontend {
public:
    bool start();                 // put the terminal in raw mode
    void stop();
    // Poll host keys; returns keyboard wire bytes to deliver (may be empty).
    std::vector<uint8_t> poll_keys();
    bool load_keymap(const std::string& path, std::string* err);
    void set_default_keymap();
    bool quit_requested = false;
    std::map<std::string, std::vector<uint8_t>> keymap;   // host key name -> BVE keyboard bytes
private:
    std::string pending_;
    bool raw_ = false;
};
