#include "cpu_glue.h"
Bus* CpuGlue::bus = nullptr;
IrqController* CpuGlue::irq = nullptr;
std::function<void(uint32_t)> CpuGlue::instr_hook;
bool CpuGlue::trace = false;
uint64_t CpuGlue::instr_count = 0;

extern "C" {
unsigned int m68k_read_memory_8(unsigned int a)  { return CpuGlue::bus->read8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return CpuGlue::bus->read16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { return CpuGlue::bus->read32(a); }
void m68k_write_memory_8(unsigned int a, unsigned int v)  { CpuGlue::bus->write8(a, (uint8_t)v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { CpuGlue::bus->write16(a, (uint16_t)v); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { CpuGlue::bus->write32(a, v); }
unsigned int m68k_read_disassembler_8(unsigned int a)  { uint32_t o; Device* d = CpuGlue::bus->find(a, &o); return d ? d->read8(o) : 0xFF; }
unsigned int m68k_read_disassembler_16(unsigned int a) { uint32_t o; Device* d = CpuGlue::bus->find(a, &o); return d ? d->read16(o) : 0xFFFF; }
unsigned int m68k_read_disassembler_32(unsigned int a) { uint32_t o; Device* d = CpuGlue::bus->find(a, &o); return d ? d->read32(o) : 0xFFFFFFFF; }
int bve_int_ack(int level) { return CpuGlue::irq ? CpuGlue::irq->acknowledge(level) : M68K_INT_ACK_SPURIOUS; }
void bve_reset_instr() { LOGI("CPU: RESET instruction executed\n"); if (CpuGlue::bus) CpuGlue::bus->reset(); }
int bve_illg(int opcode) { LOGI("CPU: illegal opcode %04x at %08x\n", opcode, m68k_get_reg(nullptr, M68K_REG_PPC)); return 0; }
void bve_instr_hook(unsigned int pc) {
    CpuGlue::instr_count++;
    if (CpuGlue::trace) { fprintf(stderr, "%s\n", CpuGlue::disasm(pc).c_str()); }
    if (CpuGlue::instr_hook) CpuGlue::instr_hook(pc);
}
}

void CpuGlue::install(Bus* b, IrqController* i) {
    bus = b; irq = i;
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68030);
    if (irq) irq->on_change = [](int level) { m68k_set_irq(level); };
}
std::string CpuGlue::disasm(uint32_t pc) {
    char buf[256]; m68k_disassemble(buf, pc, M68K_CPU_TYPE_68030);
    char out[320]; snprintf(out, sizeof out, "%08x: %-40s D0=%08x D1=%08x A0=%08x A6=%08x A7=%08x SR=%04x", pc, buf,
        m68k_get_reg(nullptr, M68K_REG_D0), m68k_get_reg(nullptr, M68K_REG_D1), m68k_get_reg(nullptr, M68K_REG_A0),
        m68k_get_reg(nullptr, M68K_REG_A6), m68k_get_reg(nullptr, M68K_REG_A7), m68k_get_reg(nullptr, M68K_REG_SR));
    return out;
}
void CpuGlue::dump_regs(FILE* f) {
    static const char* names[] = {"D0","D1","D2","D3","D4","D5","D6","D7","A0","A1","A2","A3","A4","A5","A6","A7"};
    for (int i = 0; i < 16; i++) fprintf(f, "%s=%08x%s", names[i], m68k_get_reg(nullptr, (m68k_register_t)(M68K_REG_D0 + i)), (i % 8 == 7) ? "\n" : " ");
    fprintf(f, "PC=%08x SR=%04x USP=%08x ISP=%08x MSP=%08x VBR=%08x\n", m68k_get_reg(nullptr, M68K_REG_PC), m68k_get_reg(nullptr, M68K_REG_SR),
        m68k_get_reg(nullptr, M68K_REG_USP), m68k_get_reg(nullptr, M68K_REG_ISP), m68k_get_reg(nullptr, M68K_REG_MSP), m68k_get_reg(nullptr, M68K_REG_VBR));
}
