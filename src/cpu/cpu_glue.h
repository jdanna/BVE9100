#pragma once
#include "../bus.h"
#include "../irq.h"
extern "C" {
#include "m68k.h"
}
// Binds the Musashi core to our Bus/IrqController. Exactly one Machine owns the CPU.
struct CpuGlue {
    static Bus* bus;
    static IrqController* irq;
    static std::function<void(uint32_t pc)> instr_hook;
    static bool trace;
    static uint64_t instr_count;
    static void install(Bus* b, IrqController* i);
    static std::string disasm(uint32_t pc);
    static void dump_regs(FILE* f);
};
