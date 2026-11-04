// WebAssembly entry points (built with emcmake instead of main.cpp). The web version's worker (web/worker.js) puts
// the boot loader, floppies, SRAM and EEPROM into Emscripten's file system under /bve, calls bve_init() once, then
// bve_step() to run the machine at the pace it chooses, bve_request() for the front-end's API (the same handler the
// native build serves over HTTP) and bve_save() before it copies the changed files to IndexedDB.
#include "machine.h"
#include <emscripten/emscripten.h>

static Machine* g_machine;
static Httpd::Response g_resp;

extern "C" {

EMSCRIPTEN_KEEPALIVE int bve_init() {
    MachineConfig cfg;
    cfg.boot_loader = "/bve/rms.abs";
    cfg.disk_a = "/bve/a.img";
    cfg.disk_b = "/bve/b.img";
    cfg.sram_path = "/bve/sram.bin";
    cfg.eeprom_path = "/bve/eeprom.bin";
    g_machine = new Machine(cfg);
    std::string err;
    if (!g_machine->boot(&err)) { fprintf(stderr, "boot failed: %s\n", err.c_str()); return 0; }
    g_machine->install_hooks();
    return 1;
}

EMSCRIPTEN_KEEPALIVE void bve_step(double cycles) { g_machine->step((uint64_t)cycles); }

EMSCRIPTEN_KEEPALIVE double bve_cycles() { return (double)g_cycles; }

// Returns the response body (valid until the next call); status and content type through the two getters below
EMSCRIPTEN_KEEPALIVE const char* bve_request(const char* method, const char* path, const char* body) {
    Httpd::Request r{method, path, body};
    g_resp = g_machine->handle(r);
    return g_resp.body.c_str();
}
EMSCRIPTEN_KEEPALIVE int bve_response_status() { return g_resp.status; }
EMSCRIPTEN_KEEPALIVE const char* bve_response_type() { return g_resp.type.c_str(); }

EMSCRIPTEN_KEEPALIVE void bve_save() { g_machine->save_state(); }

}
