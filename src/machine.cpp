#include "machine.h"
#include "devices/idc_peers.h"
#include "tty_frontend.h"
extern "C" unsigned int pmmu_translate_addr(unsigned int addr_in);
extern "C" unsigned int m68k_pmmu_enabled(void);
#include <csignal>
#include <ctime>
#include <chrono>
#include <thread>
#include <map>
#include <algorithm>
#include <sstream>

static volatile bool g_stop = false;
static void on_sigint(int) { g_stop = true; }

Machine::Machine(const MachineConfig& cfg) : cfg_(cfg) {
    // ---- memory -------------------------------------------------------------
    rom  = std::make_unique<Ram>("ROM",  0x10000);            // CPU-88 EPROM 9100M0 is not available: we use a stub
    sram = std::make_unique<Ram>("SRAM", 0x100000);           // 0x800000-0x8FFFFF battery-backed (512K std + DSK-6 window)
    dram = std::make_unique<Ram>("DRAM", cfg.dram_size);      // 0x1000000-
    bus.map(0x00000000, 0x10000, rom.get());
    bus.map(0x00800000, 0x100000, sram.get());
    bus.map(0x01000000, cfg.dram_size, dram.get());

    // ---- CPU-88 on-board I/O (0x0F00x000, 4 KB per device) --------------------
    // Clock assumptions: PIT1 counter 2 = 52 -> 8 MHz / 52 / 16 = 9615 baud, so both PITs are fed 8 MHz.
    pit0 = std::make_unique<Pit8254>("PIT0", 8000000.0);
    pit1 = std::make_unique<Pit8254>("PIT1", 8000000.0);
    icu0 = std::make_unique<Pic8259>("ICU0");
    icu1 = std::make_unique<Pic8259>("ICU1");
    console = std::make_unique<Usart8251>("SIO");
    dmac = std::make_unique<Dmac>("DMAC", &bus);
    rtc = std::make_unique<Rtc>();
    fdc = std::make_unique<Fdc>(dmac.get());
    scsi = std::make_unique<ScsiWd33c93>();
    misc = std::make_unique<Cpu88Misc>(fdc.get(), dmac.get());
    eeprom = std::make_unique<Eeprom>(cfg.eeprom_path);
    bus.map(0x0F001000, 0x1000, console.get());
    bus.map(0x0F002000, 0x1000, pit0.get());
    bus.map(0x0F003000, 0x1000, icu1.get());      // vectors 0x58-0x5F
    bus.map(0x0F004000, 0x1000, icu0.get());      // vectors 0x50-0x57
    bus.map(0x0F005000, 0x1000, pit1.get());
    bus.map(0x0F006000, 0x1000, rtc.get());
    bus.map(0x0F007000, 0x1000, fdc.get());
    bus.map(0x0F008000, 0x1000, dmac.get());
    bus.map(0x0F009000, 0x1000, scsi.get());
    bus.map(0x0F00A000, 0x1000, eeprom.get());
    // Cpu88Misc decodes by offset within 0x0F000000..0x0F00FFFF (pages 0, B, C, D); mapped last so the
    // device pages above take precedence.
    bus.map(0x0F000000, 0x10000, misc.get());

    // ---- DSC-63 display board ---------------------------------------------------
    dsc = std::make_unique<Dsc63>(&irq);
    bus.map(0x0D000000, 0x1D000, dsc.get());
    dsc->kbd_type = (uint8_t)cfg.kbd_type;

    // ---- SPF-4 sync/GPI board (two decodes seen in firmware) ---------------------
    spf = std::make_unique<Spf>(cfg.pal);
    bus.map(0x0FF01000, 0x1000, spf.get());
    bus.map(0x0FFF1000, 0x1000, spf.get());

    // ---- IF-508 Ethernet board (the BZE-9102 "network system" software requires it) ---------
    if (!cfg.no_if508) { if508 = std::make_unique<If508>(&irq); bus.map(0x0FF02000, 0x2000, if508.get()); }

    // ---- RS-422 interface boards (VTR / switcher ports) in option slots BKE#0..#6 ----------------
    // Port letters follow the kernel's channel numbering: port n (A = 0) is slot n/4, channel n%4.
    std::vector<MachineConfig::DeviceSpec> devs = cfg.devices;
    if (devs.empty() && !cfg.no_devices)
        devs = {{0, "vtr", 0x0101}, {1, "vtr", 0x0101}, {2, "vtr", 0x0101}, {3, "vtr", 0x0101}, {4, "switcher", 0x1001}};
    std::vector<std::pair<int,uint16_t>> boards = cfg.rs422;
    for (auto& d : devs) {
        bool have = false;
        for (auto& b : boards) if (b.first == d.port / 4) have = true;
        if (!have) boards.push_back({d.port / 4, 0x200F});
    }
    for (auto& b : boards) {
        if (b.first < 0 || b.first > 6) continue;
        rs422.push_back(std::make_unique<Rs422Board>(b.first, b.second, &irq, 3));
        bus.map(0x0FFF9000 + b.first * 0x1000, 0x1000, rs422.back().get());
        LOGI("RS422: board ID %04x in slot %d (0x%08x)\n", b.second, b.first, 0x0FFF9000 + b.first * 0x1000);
    }
    for (auto& d : devs) {
        std::shared_ptr<IdcDevice> p;
        if (d.kind == "vtr") p = std::make_shared<VirtualVtr>(d.type ? d.type : 0x0101);
        else if (d.kind == "switcher") p = std::make_shared<VirtualSwitcher>(d.type ? d.type : 0x1001);
        else if (d.kind == "probe") p = std::make_shared<IdcProbe>();
        else { fprintf(stderr, "unknown device kind '%s'\n", d.kind.c_str()); continue; }
        char port = (char)('A' + d.port);
        p->label = std::string(d.kind == "vtr" ? "VTR-" : "SW-") + port;
        for (auto& b : rs422) if (b->slot() == d.port / 4) b->attach(d.port % 4, p);
        peers.push_back({port, p});
        LOGI("RS422: port %c = %s (IDC type %04x)\n", port, d.kind.c_str(), p->idc_type());
    }

    // ---- SIO-4 RS-232C channels (ptp, tty, smc, aux) ----------------------------
    static const char* sio_names[4] = {"SIO4.PTP", "SIO4.TTY", "SIO4.SMC", "SIO4.AUX"};
    for (int i = 0; i < 4; i++) {
        sio4.push_back(std::make_unique<MailboxPort>(sio_names[i], &irq, 2, 0x400));
        bus.map(0x0FFFE000 + i * 0x400, 0x400, sio4.back().get());
    }

    // ---- interrupt wiring (IPL levels are assumptions, vectors come from the firmware) ---
    irq_icu0_ = irq.add("ICU0", 5, [this]() { return icu0->acknowledge(); });
    irq_icu1_ = irq.add("ICU1", 4, [this]() { return icu1->acknowledge(); });
    icu0->on_int = [this](bool a) { irq.set(irq_icu0_, a); };
    icu1->on_int = [this](bool a) { irq.set(irq_icu1_, a); };
    dmac->on_tc = [this](int ch) { icu0->set_irq(0, true); icu0->set_irq(0, false); };   // ICU0 IRQ0 -> 0x50 dmareq
    fdc->on_irq = [this](bool a) { icu0->set_irq(2, a); };                                // ICU0 IRQ2 -> 0x52 fdirq
    scsi->on_irq = [this](bool a) { icu0->set_irq(1, a); };                               // ICU0 IRQ1 -> 0x51 hdkirq
    pit0->on_out = [this](int ch, bool lvl) {
        if (!lvl) return;
        if (ch == 1) { icu1->set_irq(2, true); icu1->set_irq(2, false); }                 // 0x5A tup1req (system tick)
        if (ch == 2) { icu0->set_irq(3, true); icu0->set_irq(3, false); }                 // 0x53 tup3req
    };
    spf->on_field = [this]() { icu1->set_irq(3, true); icu1->set_irq(3, false); };       // 0x5B fieldint
    console->on_rxrdy = [this](bool a) { icu1->set_irq(5, a); };                          // 0x5D sioirqrxt
    console->on_tx = [](uint8_t b) { fputc(b, stdout); fflush(stdout); };

    // ---- unmapped addresses: 68030 bus error (how the firmware detects absent boards) ---
    bus.on_unmapped = [this](uint32_t a, bool w) {
        (void)a; (void)w;
        if (bus.bus_error_on_unmapped) m68k_pulse_bus_error();
    };

    CpuGlue::install(&bus, &irq);
    build_rom();
}
Machine::~Machine() {
    fdc->save_images();
    if (!cfg_.sram_path.empty()) { FILE* f = fopen(cfg_.sram_path.c_str(), "wb"); if (f) { fwrite(sram->data().data(), 1, sram->size(), f); fclose(f); LOGI("SRAM: saved %s\n", cfg_.sram_path.c_str()); } }
}

// Minimal stand-in for the CPU-88 monitor ROM: reset vector + two exception stubs.
void Machine::build_rom() {
    auto& r = rom->data();
    auto w32 = [&](uint32_t a, uint32_t v) { r[a] = v >> 24; r[a+1] = v >> 16; r[a+2] = v >> 8; r[a+3] = v; };
    auto w16 = [&](uint32_t a, uint16_t v) { r[a] = v >> 8; r[a+1] = v; };
    w32(0, 0x013FFFFE);            // initial SSP
    w32(4, 0x00000500);            // initial PC -> stub that stops (we HLE-load the boot loader instead)
    w16(rom_rte_, 0x4E73);         // RTE   (benign interrupt/trap stub)
    w16(rom_err_, 0x60FE);         // BRA.S * (fatal exception stub, detected by the instruction hook)
    w16(0x500, 0x4E72); w16(0x502, 0x2700);   // STOP #$2700
}
// The monitor normally leaves a vector table in low DRAM and VBR pointing at it.
void Machine::setup_vectors() {
    const uint32_t vbr = 0x01000000;
    for (int v = 0; v < 256; v++) {
        uint32_t target = rom_err_;
        if (v >= 24 && v <= 31) target = rom_rte_;      // autovectors
        if (v >= 32 && v <= 47) target = rom_rte_;      // TRAP #0-15: the kernel calls the monitor with TRAP #14 (debug hooks) -> ignore
        if (v >= 64) target = rom_rte_;                 // user vectors
        bus.write32(vbr + v * 4, target);
    }
    m68k_set_reg(M68K_REG_VBR, vbr);
}
void Machine::place_module(const AbsModule& m) {
    bus.write_block(m.text_addr, m.text.data(), (uint32_t)m.text.size());
    bus.write_block(m.data_addr, m.data.data(), (uint32_t)m.data.size());
    LOGI("LOAD: '%.20s' text %08x (%zu) data %08x (%zu) entry %08x\n", m.version.c_str(), m.text_addr, m.text.size(), m.data_addr, m.data.size(), m.real_entry());
}
bool Machine::boot(std::string* err) {
    {   // Battery-backed SRAM: a never-initialised chip holds random data, which makes the application
        // reject the backup database checksum and load its defaults. Zero-filled SRAM passes the check with
        // empty tables (no key assignments), so start from pseudo-random content unless an image exists.
        FILE* f = cfg_.sram_path.empty() ? nullptr : fopen(cfg_.sram_path.c_str(), "rb");
        if (f) { fread(sram->data().data(), 1, sram->size(), f); fclose(f); LOGI("SRAM: loaded %s\n", cfg_.sram_path.c_str()); }
        else { uint32_t x = 0x12345678; for (auto& b : sram->data()) { x = x * 1103515245u + 12345u; b = (uint8_t)(x >> 16); } }
    }
    if (!cfg_.disk_a.empty()) fdc->load_image(0, cfg_.disk_a, false);
    if (!cfg_.disk_b.empty()) fdc->load_image(1, cfg_.disk_b, false);
    m68k_pulse_reset();
    setup_vectors();
    AbsModule bl;
    if (cfg_.boot_loader.empty()) { *err = "no boot loader given (--boot RMS.SYS); the CPU-88 ROM is not available"; return false; }
    if (!load_abs(cfg_.boot_loader, bl, err)) return false;
    place_module(bl);
    for (auto& p : cfg_.modules) { AbsModule m; if (!load_abs(p, m, err)) return false; place_module(m); }
    for (auto& s : bl.symbols) syms_.push_back(s);
    for (auto& p : cfg_.symbol_files) { AbsModule m; if (load_abs(p, m, err)) for (auto& s : m.symbols) syms_.push_back(s); else LOGI("symbols: %s\n", err->c_str()); }
    LOGI("symbols: %zu loaded\n", syms_.size());
    m68k_set_reg(M68K_REG_SR, 0x2700);
    m68k_set_reg(M68K_REG_SP, 0x013FFFFE);
    m68k_set_reg(M68K_REG_PC, bl.real_entry());
    // Registers the monitor passes: D7 = boot drive? A0 = parameter block? (unknown; zeros for now)
    LOGI("BOOT: PC=%08x SP=%08x VBR=%08x\n", bl.real_entry(), 0x013FFFFE, 0x01000000);
    return true;
}
// Name of the running SRMS task: kernel variable `etcb` -> TCB, byte +0x49 = PCB number, names at syspcb+0x80*n+0x18... (V03L20 layout)
std::string Machine::current_task() {
    static uint32_t etcb_addr = 0;
    if (!etcb_addr) { for (auto& s : syms_) if (s.name == "_etcb" || s.name == "etcb") etcb_addr = s.addr; if (!etcb_addr) etcb_addr = 0x01059cba; }   // SRMS V03L20 `_etcb`
    uint32_t tcb = bus.read32(etcb_addr);
    if (tcb < 0x01000000 || tcb >= 0x01400000) return " [idle]";
    uint8_t pcbno = bus.read8(tcb + 0x49);
    char name[20] = {0};
    for (int i = 0; i < 16; i++) name[i] = (char)bus.read8(0x010557b0 + pcbno * 0x80 + i);
    for (int i = 15; i >= 0 && name[i] == ' '; i--) name[i] = 0;
    char buf[48]; snprintf(buf, sizeof buf, " [%s]", (name[0] >= 0x20 && name[0] < 0x7f) ? name : "kernel");
    return buf;
}
// The application keeps its key-assignment table (768 x 16-bit, indexed by keyboard code with 0x100/0x200
// shift planes; 0xFFFF = unassigned) behind the pointer at 0xD0096434 and the factory defaults behind
// 0xD0096438 (CMNDB shared data). A fresh backup database leaves the live table empty, and only the
// INIT MENU "KEY ASSIGN" -> "KEY ASSIGN INITIALIZED" confirmation (MAIN FUN_8007e802) copies the defaults
// in. Do that copy here once MAIN is running.
void Machine::hle_keyassign_default() {
    if (keyassign_done_ || !m68k_pmmu_enabled()) return;
    auto xl = [&](uint32_t la) { return (la >= 0x80000000) ? pmmu_translate_addr(la & ~0x3FFu) + (la & 0x3FF) : la; };
    uint32_t p_src = xl(0xD0096438), p_dst = xl(0xD0096434);
    if (p_src < 0x01000000 || p_dst < 0x01000000) return;
    uint32_t src = bus.read32(p_src), dst = bus.read32(p_dst);
    if (src < 0x80000000 || dst < 0x80000000 || src == dst) { LOGI("HLE: key assignment pointers not ready (src %08x dst %08x)\n", src, dst); keyassign_done_ = true; return; }
    for (uint32_t i = 0; i < 0x600; i += 2) bus.write16(xl(dst + i), bus.read16(xl(src + i)));
    keyassign_done_ = true;
    LOGI("HLE: default key assignment table copied (%08x -> %08x)\n", src, dst);
}
std::string Machine::symbolize(uint32_t addr) const {
    const AbsModule::Sym* best = nullptr;
    for (auto& s : syms_) if (s.addr <= addr && (!best || s.addr > best->addr)) best = &s;
    char buf[96];
    if (best && addr - best->addr < 0x4000) snprintf(buf, sizeof buf, "%08x <%s+%x>", addr, best->name.c_str(), addr - best->addr);
    else snprintf(buf, sizeof buf, "%08x", addr);
    return buf;
}
void Machine::dump_screen(FILE* f) { dsc->dump_screen(f, cfg_.color); dsc->screen_dirty = false; }

void Machine::run() {
    signal(SIGINT, on_sigint);
    const uint32_t slice = 2000;           // 100 us of CPU time per slice
    uint32_t err_pc = rom_err_;
    CpuGlue::instr_hook = [this, err_pc](uint32_t pc) {
        if (pc == err_pc) {
            uint32_t sp = m68k_get_reg(nullptr, M68K_REG_SP);
            uint16_t sr = (uint16_t)bus.read16(sp), fmt = (uint16_t)bus.read16(sp + 6);
            uint32_t ppc = bus.read32(sp + 2);
            fprintf(stderr, "\n*** EXCEPTION: vector %d (offset %03x) at PC=%08x SR=%04x frame format %x\n", (fmt & 0xFFF) / 4, fmt & 0xFFF, ppc, sr, fmt >> 12);
            if (((fmt & 0xFFF) / 4) == 2) fprintf(stderr, "    bus error, fault address %08x\n", bus.read32(sp + 16));
            CpuGlue::dump_regs(stderr);
            fprintf(stderr, "    %s  %s\n", CpuGlue::disasm(ppc).c_str(), symbolize(ppc).c_str());
            halted_ = true;
        }
        if (cfg_.profile && (CpuGlue::instr_count & 0x3FF) == 0) {
            auto it = std::lower_bound(pc_hist_.begin(), pc_hist_.end(), std::make_pair(pc, (uint64_t)0));
            if (it != pc_hist_.end() && it->first == pc) it->second++; else pc_hist_.insert(it, {pc, 1});
        }
        if (cfg_.trace_from && pc == cfg_.trace_from) CpuGlue::trace = true;
        if (cfg_.keyassign_default && !keyassign_done_ && pc == 0x01010e98 && g_cycles > 1500000000ull && current_task().find("MAIN") != std::string::npos) hle_keyassign_default();
        for (auto t : cfg_.tracepoints) if (pc == t) {
            uint32_t sp = m68k_get_reg(nullptr, M68K_REG_SP);
            // in user mode the C arguments sit on the user stack at logical addresses (translate through the PMMU)
            auto rd32 = [&](uint32_t la) { uint32_t pa = la; if (la >= 0x80000000 && m68k_pmmu_enabled()) pa = pmmu_translate_addr(la & ~0x3FFu) + (la & 0x3FF); return bus.read32(pa); };
            fprintf(stderr, "[%10llu] TP %s%s  D0=%08x D1=%08x D7=%08x A0=%08x SP=%08x args=%08x %08x %08x %08x %08x %08x ret=%08x\n", (unsigned long long)g_cycles, symbolize(pc).c_str(), current_task().c_str(),
                m68k_get_reg(nullptr, M68K_REG_D0), m68k_get_reg(nullptr, M68K_REG_D1), m68k_get_reg(nullptr, M68K_REG_D7), m68k_get_reg(nullptr, M68K_REG_A0), sp,
                rd32(sp + 4), rd32(sp + 8), rd32(sp + 12), rd32(sp + 16), rd32(sp + 20), rd32(sp + 24), rd32(sp));
            {   // block addressed by A0 (kernel syscall parameter block) and the pointer stored at A0+8 (message)
                uint32_t a0 = m68k_get_reg(nullptr, M68K_REG_A0);
                if (a0 >= 0x80000000 && m68k_pmmu_enabled()) a0 = pmmu_translate_addr(a0);
                if (a0 >= 0x01000000 && a0 < 0x01000000 + cfg_.dram_size - 32) {
                    fprintf(stderr, "             *A0  :"); for (int i = 0; i < 24; i++) fprintf(stderr, " %02x", bus.read8(a0 + i)); fprintf(stderr, "\n");
                    for (int po = 8; po <= 12; po += 4) {
                        uint32_t p = bus.read32(a0 + po);
                        if (p >= 0x80000000 && m68k_pmmu_enabled()) p = pmmu_translate_addr(p);
                        if (p >= 0x01000000 && p < 0x01000000 + cfg_.dram_size - 48) {
                            fprintf(stderr, "             *A0+%d:", po); for (int i = 0; i < 40; i++) fprintf(stderr, " %02x", bus.read8(p + i));
                            fprintf(stderr, "  '"); for (int i = 0; i < 40; i++) { uint8_t c = bus.read8(p + i); fputc((c >= 0x20 && c < 0x7f) ? c : '.', stderr); } fprintf(stderr, "'\n");
                            uint32_t q = bus.read32(p + 2);                       // SRMS message: +2 = payload pointer
                            if (q >= 0x80000000 && m68k_pmmu_enabled()) q = pmmu_translate_addr(q & ~0x3FFu) + (q & 0x3FF);
                            if (q >= 0x01000000 && q < 0x01000000 + cfg_.dram_size - 16) { fprintf(stderr, "             payload:"); for (int i = 0; i < 16; i++) fprintf(stderr, " %02x", bus.read8(q + i)); fprintf(stderr, "\n"); }
                        }
                    }
                }
            }
            {   // user-mode call chain: return addresses found on the user stack (application code is at 0x8xxxxxxx / 0xCxxxxxxx)
                uint32_t usp = m68k_get_reg(nullptr, M68K_REG_USP);
                fprintf(stderr, "             USP=%08x stack:", usp);
                for (int i = 0; i < 24 && m68k_pmmu_enabled(); i++) {
                    uint32_t la = usp + 4 * i, pa = pmmu_translate_addr(la & ~0x3FFu) + (la & 0x3FF);
                    uint32_t v = bus.read32(pa);
                    if ((v >> 28) == 0x8 || (v >> 28) == 0xC) fprintf(stderr, " %08x", v);
                }
                fprintf(stderr, "\n");
            }
            for (auto& td : cfg_.tp_dumps) {
                fprintf(stderr, "             mem %08x:", td.first);
                for (uint32_t i = 0; i < td.second; i++) {
                    uint32_t la = td.first + i, pa = la;
                    if (la >= 0x80000000 && m68k_pmmu_enabled()) pa = pmmu_translate_addr(la & ~0x3FFu) + (la & 0x3FF);
                    uint32_t off; Device* d = bus.find(pa, &off);
                    if (i % 32 == 0) fprintf(stderr, "\n               %08x ", la);
                    fprintf(stderr, "%02x", d ? d->read8(off) : 0xFF);
                }
                fprintf(stderr, "\n");
            }
            for (int ai = 1; ai <= 3; ai++) {
                uint32_t a2 = rd32(sp + 4 * ai);
                if (a2 >= 0x80000000 && m68k_pmmu_enabled()) a2 = pmmu_translate_addr(a2 & ~0x3FFu) + (a2 & 0x3FF);
                if (a2 >= 0x01000000 && a2 < 0x01000000 + cfg_.dram_size - 16) {
                    fprintf(stderr, "             *arg%d:", ai); for (int i = 0; i < 16; i++) fprintf(stderr, " %02x", bus.read8(a2 + i));
                    fprintf(stderr, "  '"); for (int i = 0; i < 16; i++) { uint8_t c = bus.read8(a2 + i); fputc((c >= 0x20 && c < 0x7f) ? c : '.', stderr); } fprintf(stderr, "'\n");
                }
            }
        }
        for (auto b : cfg_.breakpoints) if (pc == b) { fprintf(stderr, "*** BREAKPOINT %08x\n", pc); CpuGlue::dump_regs(stderr); halted_ = true; }
    };
    auto wall_start = std::chrono::steady_clock::now();
    TtyFrontend tty;
    if (cfg_.tty) {
        tty.set_default_keymap();
        if (!cfg_.keymap.empty()) { std::string e; if (!tty.load_keymap(cfg_.keymap, &e)) fprintf(stderr, "%s\n", e.c_str()); }
        if (!tty.start()) { fprintf(stderr, "stdin is not a terminal; --tty disabled\n"); cfg_.tty = false; }
        else fprintf(stdout, "\033[2J");
    }
    uint64_t last_poll = 0, last_draw = 0, last_http = 0;
    if (cfg_.http_port) {
        if (!httpd_.start(cfg_.http_port, cfg_.ui_root)) fprintf(stderr, "HTTP: cannot listen on port %d\n", cfg_.http_port);
        else fprintf(stderr, "HTTP: front-end at http://127.0.0.1:%d/  (ui files from %s/)\n", cfg_.http_port, cfg_.ui_root.c_str());
        httpd_.handler = [this](const Httpd::Request& r) {
            Httpd::Response resp;
            if (r.path == "/screen") { resp.type = "application/json"; resp.body = dsc->screen_json(); }
            else if (r.path == "/dial" && r.method == "POST") {   // body: signed dial counts, e.g. "+12" or "-3"
                int n = atoi(r.body.c_str());
                dsc->dial_add(n);
                char b[48]; snprintf(b, sizeof b, "{\"dial\":%d}", dsc->dial_count());
                resp.type = "application/json"; resp.body = b;
            }
            else if (r.path == "/devices") {                 // state of the virtual RS-422 devices
                std::string j = "[";
                for (auto& pp : peers) {
                    std::string st = pp.peer->status_json();
                    if (j.size() > 1) j += ",";
                    j += "{\"port\":\"" + std::string(1, pp.port) + "\"," + st.substr(1);
                }
                resp.type = "application/json"; resp.body = j + "]";
            }
            else if (r.path.rfind("/devices/", 0) == 0 && r.method == "POST") {   // body: "<command> [arg]"
                char port = r.path.size() > 9 ? (char)toupper(r.path[9]) : 0;
                std::string cmd = r.body, arg;
                while (!cmd.empty() && isspace((unsigned char)cmd.back())) cmd.pop_back();
                size_t sp = cmd.find(' '); if (sp != std::string::npos) { arg = cmd.substr(sp + 1); cmd = cmd.substr(0, sp); }
                bool ok = false;
                for (auto& pp : peers) if (pp.port == port) ok = pp.peer->control(cmd, arg);
                resp.status = ok ? 200 : 400; resp.body = ok ? "ok" : "unknown device or command";
            }
            else if (r.path == "/status") {
                char b[256]; snprintf(b, sizeof b, "{\"cycles\":%llu,\"seconds\":%.2f,\"leds\":%u,\"realtime\":%s}", (unsigned long long)g_cycles, (double)g_cycles / CPU_HZ, misc->leds, cfg_.realtime ? "true" : "false");
                resp.type = "application/json"; resp.body = b;
            }
            else if (r.path == "/key" && r.method == "POST") {       // body: hex bytes, e.g. "82 03 57"
                std::vector<uint8_t> bytes; std::istringstream ss(r.body); std::string tok;
                while (ss >> tok) bytes.push_back((uint8_t)strtoul(tok.c_str(), nullptr, 16));
                if (!bytes.empty()) { dsc->keyboard.deliver(bytes); LOGD("HTTP: key bytes delivered (%zu)\n", bytes.size()); }
                resp.body = "ok";
            }
            else { resp.status = 404; resp.body = "unknown endpoint"; }
            return resp;
        };
    }
    while (!g_stop && !halted_) {
        int done = m68k_execute(slice);
        g_cycles += (uint32_t)done;
        bus.tick((uint32_t)done);
        if (cfg_.tty && g_cycles - last_poll > CPU_HZ / 100) {          // every 10 ms emulated
            last_poll = g_cycles;
            auto keys = tty.poll_keys();
            if (!keys.empty()) dsc->keyboard.deliver(keys);
            if (tty.quit_requested) break;
            if (dsc->screen_dirty && g_cycles - last_draw > CPU_HZ / 20) {
                last_draw = g_cycles; fprintf(stdout, "\033[H"); dump_screen(stdout);
                fprintf(stdout, "\033[K t=%.1fs  Ctrl-] quits\n", (double)g_cycles / CPU_HZ); fflush(stdout);
            }
        }
        if (cfg_.http_port && g_cycles - last_http > CPU_HZ / 200) { last_http = g_cycles; httpd_.poll(); }
        if (cfg_.turbo_until && g_cycles >= cfg_.turbo_until && !cfg_.realtime) { cfg_.realtime = true; wall_start = std::chrono::steady_clock::now() - std::chrono::microseconds((long)((double)g_cycles / CPU_HZ * 1e6)); }
        if (cfg_.realtime) {
            double target = (double)g_cycles / CPU_HZ;
            double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall_start).count();
            if (target > wall + 0.002) std::this_thread::sleep_for(std::chrono::microseconds((long)((target - wall) * 1e6)));
        }
        if (cfg_.max_cycles && g_cycles >= cfg_.max_cycles) break;
        for (auto& sw : cfg_.swaps) if (!sw.done && g_cycles >= sw.cycle) {
            sw.done = true; fdc->save_images();
            if (fdc->load_image(sw.drive, sw.image, false)) misc->set_disk_changed(sw.drive);
            LOGI("FDC: disk swapped in drive %c -> %s\n", 'A' + sw.drive, sw.image.c_str());
        }
        for (auto& k : cfg_.key_events) {
            if (!k.bytes.empty() && g_cycles >= k.cycle) { dsc->keyboard.deliver(k.bytes); LOGI("KEY: injected %zu bytes\n", k.bytes.size()); k.bytes.clear(); }
            if (k.dial && g_cycles >= k.cycle) { dsc->dial_add(k.dial); LOGI("DIAL: %+d -> %d\n", k.dial, dsc->dial_count()); k.dial = 0; }
        }
        if (cfg_.screen_every_ms && dsc->screen_dirty && g_cycles - last_screen_ > (uint64_t)cfg_.screen_every_ms * (CPU_HZ / 1000)) {
            last_screen_ = g_cycles; fprintf(stdout, "\033[H\033[2J"); dump_screen(stdout);
        }
    }
    if (cfg_.tty) tty.stop();
    httpd_.stop();
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall_start).count();
    fprintf(stderr, "\n--- stopped after %llu cycles (%.2f s emulated, %.2f s wall), %llu instructions\n",
            (unsigned long long)g_cycles, (double)g_cycles / CPU_HZ, secs, (unsigned long long)CpuGlue::instr_count);
    fprintf(stderr, "LEDs/7seg latch: %02x\n", misc->leds);
    if (cfg_.profile) {
        std::vector<std::pair<uint32_t, uint64_t>> h = pc_hist_;
        std::sort(h.begin(), h.end(), [](auto& a, auto& b) { return a.second > b.second; });
        fprintf(stderr, "--- PC profile (samples every 1024 instructions):\n");
        for (size_t i = 0; i < h.size() && i < 25; i++) fprintf(stderr, "%8llu  %s   %s\n", (unsigned long long)h[i].second, symbolize(h[i].first).c_str(), CpuGlue::disasm(h[i].first).substr(10, 40).c_str());
    }
    fprintf(stderr, "PC=%s\n", symbolize(m68k_get_reg(nullptr, M68K_REG_PC)).c_str());
    dump_screen(stdout);
    if (!cfg_.vram_dump.empty()) dsc->save_vram(cfg_.vram_dump);
    for (auto& spec : cfg_.mem_dumps) {
        size_t c1 = spec.find(':'), c2 = spec.find(':', c1 + 1);
        if (c1 == std::string::npos || c2 == std::string::npos) continue;
        uint32_t addr = (uint32_t)strtoul(spec.substr(0, c1).c_str(), nullptr, 16), len = (uint32_t)strtoul(spec.substr(c1 + 1, c2 - c1 - 1).c_str(), nullptr, 16);
        FILE* f = fopen(spec.substr(c2 + 1).c_str(), "wb"); if (!f) continue;
        for (uint32_t i = 0; i < len; i++) { uint32_t off; Device* d = bus.find(addr + i, &off); fputc(d ? d->read8(off) : 0xFF, f); }
        fclose(f); LOGI("MEM: dumped %08x+%x to %s\n", addr, len, spec.substr(c2 + 1).c_str());
    }
}
