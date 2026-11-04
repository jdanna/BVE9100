#pragma once
#include "bus.h"
#include "irq.h"
#include "loader.h"
#include "httpd.h"
#include "cpu/cpu_glue.h"
#include "devices/pit8254.h"
#include "devices/pic8259.h"
#include "devices/usart8251.h"
#include "devices/dmac.h"
#include "devices/rtc.h"
#include "devices/fdc.h"
#include "devices/scsi_wd33c93.h"
#include "devices/rs422.h"
#include "devices/cpu88_misc.h"
#include "devices/dsc63.h"
#include "devices/spf.h"
#include "devices/eeprom.h"
#include "devices/mailbox.h"
#include "devices/if508.h"
#include <memory>

struct MachineConfig {
    std::string disk_a, disk_b;          // floppy images (raw 1474560-byte FAT12)
    std::string boot_loader;             // RMS.SYS (raw .abs) to HLE-load instead of the ROM
    std::vector<std::string> modules;    // extra raw .abs modules to place in memory (debug)
    std::string eeprom_path;
    std::string sram_path;               // battery-backed SRAM image (persists the !BUPDB setup database)
    bool pal = false;
    uint32_t dram_size = 4 << 20;        // 4 MB standard (BDRAM option: 16 MB)
    uint64_t max_cycles = 0;             // stop after N cycles (0 = run forever)
    int screen_every_ms = 0;             // dump text screen periodically (0 = only at exit)
    bool color = true;
    uint32_t trace_from = 0;             // start instruction trace when PC hits this (0 = off)
    std::vector<uint32_t> breakpoints;
    std::vector<uint32_t> tracepoints;     // print a line (no halt) when PC hits one of these
    std::vector<std::pair<uint32_t,uint32_t>> tp_dumps;   // logical ADDR,LEN dumped (MMU-translated) at every tracepoint hit
    std::vector<std::string> symbol_files;   // raw .abs files whose symbol tables name addresses in reports
    bool profile = false;
    std::string vram_dump;               // write DSC-63 VRAM here at exit
    struct KeyEvent { uint64_t cycle; std::vector<uint8_t> bytes; int dial = 0; };
    std::vector<KeyEvent> key_events;    // scripted keyboard bytes delivered at a cycle
    struct PokeEvent { uint64_t cycle; uint32_t addr; std::vector<uint8_t> bytes; bool done = false; };
    std::vector<PokeEvent> pokes;        // debug: bytes written to physical memory at a cycle (--poke)
    bool tty = false;                    // interactive terminal front-end
    bool realtime = false;               // pace emulation to wall-clock time (20 MHz)
    bool no_if508 = false;               // omit the IF-508 network board
    int kbd_type = 0x43;                 // byte answered to the keyboard 'R','Z' request
    bool keyassign_default = true;       // HLE: load the application's default key assignment table (as the INIT menu would)
    bool swmem = true;                   // HLE: describe the virtual switcher to the editor's switcher library (SWMEM, PVW / MON SEL)
    bool idle_skip = true;               // tick the devices straight to the next interrupt while the kernel idles (--no-idle-skip)
    std::string keymap;
    int http_port = 0;                   // browser front-end (0 = off)
    std::string ui_root = "ui";
    uint64_t turbo_until = 0;            // run flat out until this cycle, then pace to real time (used with --http)
    std::vector<std::string> mem_dumps;  // ADDR:LEN:FILE written at exit
    struct SwapEvent { uint64_t cycle; int drive; std::string image; bool done = false; };
    std::vector<SwapEvent> swaps;        // floppy changes at a given cycle
    std::vector<std::pair<int,uint16_t>> rs422;   // RS-422 interface boards: option slot, board ID
    struct DeviceSpec { int port; std::string kind; uint16_t type; };   // port 0 = 'A' = slot 0 channel 0
    std::vector<DeviceSpec> devices;     // virtual devices on RS-422 ports (default: VTRs on A-D, switcher on E)
    bool no_devices = false;
    std::string gpi_udp = "127.0.0.1:49100";   // GPI outputs as "gpi N" UDP datagrams (the MAXINE! emulator); "" = off
    int gpi_base = 1;                    // GPI port 0 is sent as input gpi_base (MAXINE inputs are 1-8)
};

class Machine {
public:
    explicit Machine(const MachineConfig& cfg);
    ~Machine();
    bool boot(std::string* err);
    void run();
    void install_hooks();                  // instruction hook (run() installs it)
    void step(uint64_t cycles);            // run this many CPU cycles (WebAssembly build)
    void save_state();                     // floppies, SRAM, EEPROM to their files
    Httpd::Response handle(const Httpd::Request& r);   // front-end API: /screen, /key, /dial, /devices, /palette, ...
    void dump_screen(FILE* f);
    Bus bus; IrqController irq;
    std::unique_ptr<Ram> rom, sram, dram;
    std::unique_ptr<Pit8254> pit0, pit1;
    std::unique_ptr<Pic8259> icu0, icu1;
    std::unique_ptr<Usart8251> console;
    std::unique_ptr<Dmac> dmac;
    std::unique_ptr<Rtc> rtc;
    std::unique_ptr<Fdc> fdc;
    std::unique_ptr<ScsiWd33c93> scsi;
    std::unique_ptr<Cpu88Misc> misc;
    std::unique_ptr<Dsc63> dsc;
    std::unique_ptr<Spf> spf;
    std::unique_ptr<Eeprom> eeprom;
    std::vector<std::unique_ptr<MailboxPort>> sio4;
    std::unique_ptr<If508> if508;
    std::vector<std::unique_ptr<Rs422Board>> rs422;
    struct PortPeer { char port; std::shared_ptr<SerialPeer> peer; };
    std::vector<PortPeer> peers;         // virtual devices, for the HTTP /devices endpoint
    void place_module(const AbsModule& m);
    std::string symbolize(uint32_t addr) const;
    std::string current_task();
    bool keyassign_done_ = false, swmem_done_ = false;
    // GPI outputs (the four standard ports on the SPF board): state, pulses fired, and the UDP target
    bool gpi_on_[4] = {false, false, false, false};
    unsigned gpi_count_[4] = {0, 0, 0, 0};
    void gpi_out(int port, bool on);
    int gpi_sock_ = -1;
    Httpd httpd_;
    void hle_keyassign_default();
    void hle_switcher_profile();
    void hle_xpt_defaults();
    bool cpu_idle();                     // the CPU sits in the kernel's idle loop with no interrupt to take
    bool idle_loop_at(uint32_t a);
    uint32_t idle_lo_ = 0;               // start of the idle loop once found
    std::vector<AbsModule::Sym> syms_;
    std::vector<std::pair<uint32_t, uint64_t>> pc_hist_;
private:
    void build_rom();
    void setup_vectors();
    MachineConfig cfg_;
    int irq_icu0_, irq_icu1_;
    uint32_t rom_rte_ = 0x400, rom_err_ = 0x410;
    bool halted_ = false;
    uint64_t last_screen_ = 0;
};
