#include "machine.h"
#include <cstring>
#include <cstdlib>

static void usage() {
    fprintf(stderr,
        "bve9100 - Sony BVE-9100 editing controller emulator (work in progress)\n"
        "usage: bve9100 --boot RMS.SYS [--disk-a img] [--disk-b img] [options]\n"
        "  --boot FILE        raw (LHA-unpacked) RMS.SYS boot loader .abs, HLE-loaded in place of the CPU-88 ROM\n"
        "  --disk-a/--disk-b  1.44 MB FAT12 floppy images (tools/mkfat12.py builds them from the installer files)\n"
        "  --module FILE      place an additional raw .abs module in memory before start (repeatable)\n"
        "  --eeprom FILE      persist the 2 KB setup EEPROM here\n"
        "  --sram FILE        persist the battery-backed SRAM (setup database) here\n"
        "  --dram MB          DRAM size (4 or 16)\n"
        "  --pal              50 Hz reference instead of 59.94 Hz\n"
        "  --no-if508         do not emulate the IF-508 network board\n"
        "  --no-keyassign     do not load the default key-assignment table into the application\n"
        "  --cycles N         stop after N CPU cycles\n"
        "  --screen MS        redraw the text display every MS milliseconds of emulated time\n"
        "  --trace            instruction trace from the start\n"
        "  --trace-from HEX   instruction trace once PC reaches address\n"
        "  --break HEX        halt and dump registers at address (repeatable)\n"
        "  --tp HEX           tracepoint: log registers/args when PC reaches address, continue (repeatable)\n"
        "  --syms FILE        raw .abs whose symbol table is used to name addresses (repeatable)\n"
        "  --profile          sample PC and print the hottest locations at exit\n"
        "  --key CYC:HH,HH    deliver raw keyboard bytes at the given cycle (repeatable)\n"
        "  --swap CYC:D:IMG   change the floppy in drive D (A/B) to image IMG at the given cycle\n"
        "  --tty              interactive: keyboard from the terminal, live screen redraw (Ctrl-] quits)\n"
        "  --http PORT        browser front-end on http://127.0.0.1:PORT/ (screen + on-screen BKE-9400A keyboard)\n"
        "  --ui-root DIR      directory with the front-end files (default ./ui)\n"
        "  --turbo-until N    run flat out until cycle N, then pace to real time (default with --http: 2700000000)\n"
        "  --realtime         pace the emulation to real time (default: as fast as possible)\n"
        "  --keymap FILE      host key -> keyboard byte mapping (see keymap.txt)\n"
        "  --vram-dump FILE   write the DSC-63 VRAM (for analysis) at exit\n"
        "  --rs422 SLOT[:ID]  add an RS-422 interface board (option slot 0-6, board ID hex, default 200F)\n"
        "  --device P=KIND[:TYPE]  virtual device on RS-422 port P (A..): vtr, switcher (IDC type hex);\n"
        "                     default: VTRs on A-D, switcher on E\n"
        "  --no-devices       no RS-422 boards or virtual devices\n"
        "  --mem-dump A:L:F   write L bytes of physical memory from A (hex) to file F at exit (repeatable)\n"
        "  --buserr           raise a 68030 bus error on unmapped addresses (default: read as 0xFF, open bus)\n"
        "  --no-color         plain text screen dump\n"
        "  -v / -vv / -vvv    verbosity\n");
}
int main(int argc, char** argv) {
    MachineConfig cfg; bool trace = false, buserr = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { if (i + 1 >= argc) { usage(); exit(2); } return argv[++i]; };
        if (a == "--boot") cfg.boot_loader = next();
        else if (a == "--disk-a") cfg.disk_a = next();
        else if (a == "--disk-b") cfg.disk_b = next();
        else if (a == "--module") cfg.modules.push_back(next());
        else if (a == "--eeprom") cfg.eeprom_path = next();
        else if (a == "--sram") cfg.sram_path = next();
        else if (a == "--dram") cfg.dram_size = (uint32_t)atoi(next()) << 20;
        else if (a == "--pal") cfg.pal = true;
        else if (a == "--no-if508") cfg.no_if508 = true;
        else if (a == "--kbd-type") cfg.kbd_type = (int)strtoul(next(), nullptr, 16);
        else if (a == "--no-keyassign") cfg.keyassign_default = false;
        else if (a == "--syms") cfg.symbol_files.push_back(next());
        else if (a == "--profile") cfg.profile = true;
        else if (a == "--vram-dump") cfg.vram_dump = next();
        else if (a == "--tty") cfg.tty = true;
        else if (a == "--http") { cfg.http_port = atoi(next()); if (!cfg.turbo_until) cfg.turbo_until = 2700000000ull; }
        else if (a == "--ui-root") cfg.ui_root = next();
        else if (a == "--turbo-until") cfg.turbo_until = strtoull(next(), nullptr, 0);
        else if (a == "--swap") {           // --swap CYCLE:DRIVE:IMAGE  change the floppy in drive A/B at a cycle
            std::string v = next(); size_t c1 = v.find(':'), c2 = v.find(':', c1 + 1);
            if (c1 == std::string::npos || c2 == std::string::npos) { usage(); return 2; }
            MachineConfig::SwapEvent ev; ev.cycle = strtoull(v.substr(0, c1).c_str(), nullptr, 0);
            ev.drive = (toupper(v[c1 + 1]) == 'B') ? 1 : 0; ev.image = v.substr(c2 + 1); cfg.swaps.push_back(ev);
        }
        else if (a == "--mem-dump") cfg.mem_dumps.push_back(next());
        else if (a == "--realtime") cfg.realtime = true;
        else if (a == "--device") {         // --device PORT=KIND[:TYPE]  e.g. A=vtr, E=switcher:1001
            std::string v = next(); size_t e = v.find('='), c = v.find(':');
            if (e != 1 || !isalpha((unsigned char)v[0])) { usage(); return 2; }
            MachineConfig::DeviceSpec d; d.port = toupper(v[0]) - 'A';
            d.kind = v.substr(2, c == std::string::npos ? std::string::npos : c - 2);
            d.type = (uint16_t)(c == std::string::npos ? 0 : strtoul(v.substr(c + 1).c_str(), nullptr, 16));
            cfg.devices.push_back(d);
        }
        else if (a == "--no-devices") cfg.no_devices = true;
        else if (a == "--rs422") {          // --rs422 SLOT[:ID]  RS-422 interface board in option slot 0-6 (ID hex, default 200F)
            std::string v = next(); size_t c = v.find(':');
            cfg.rs422.push_back({atoi(v.substr(0, c).c_str()), (uint16_t)(c == std::string::npos ? 0x200F : strtoul(v.substr(c + 1).c_str(), nullptr, 16))});
        }
        else if (a == "--keymap") cfg.keymap = next();
        else if (a == "--key") {            // --key CYCLE:HH[,HH...]  raw keyboard bytes delivered to the DSC-63 keyboard port
            std::string v = next(); size_t c = v.find(':');
            MachineConfig::KeyEvent ev; ev.cycle = strtoull(v.substr(0, c).c_str(), nullptr, 0);
            for (size_t i = c + 1; i < v.size();) { size_t e = v.find(',', i); if (e == std::string::npos) e = v.size(); ev.bytes.push_back((uint8_t)strtoul(v.substr(i, e - i).c_str(), nullptr, 16)); i = e + 1; }
            cfg.key_events.push_back(ev);
        }
        else if (a == "--cycles") cfg.max_cycles = strtoull(next(), nullptr, 0);
        else if (a == "--screen") cfg.screen_every_ms = atoi(next());
        else if (a == "--trace") trace = true;
        else if (a == "--trace-from") cfg.trace_from = (uint32_t)strtoul(next(), nullptr, 16);
        else if (a == "--break") cfg.breakpoints.push_back((uint32_t)strtoul(next(), nullptr, 16));
        else if (a == "--tp") cfg.tracepoints.push_back((uint32_t)strtoul(next(), nullptr, 16));
        else if (a == "--tp-dump") { std::string v = next(); size_t c = v.find(':'); cfg.tp_dumps.push_back({(uint32_t)strtoul(v.substr(0, c).c_str(), nullptr, 16), (uint32_t)strtoul(v.substr(c + 1).c_str(), nullptr, 16)}); }
        else if (a == "--buserr") buserr = true;
        else if (a == "--no-color") cfg.color = false;
        else if (a == "-v") g_verbose = 1; else if (a == "-vv") g_verbose = 2; else if (a == "-vvv") g_verbose = 3;
        else if (a == "-q") g_verbose = 0;
        else { usage(); return 2; }
    }
    Machine m(cfg);
    m.bus.bus_error_on_unmapped = buserr;
    CpuGlue::trace = trace;
    std::string err;
    if (!m.boot(&err)) { fprintf(stderr, "boot failed: %s\n", err.c_str()); usage(); return 1; }
    m.run();
    return 0;
}
