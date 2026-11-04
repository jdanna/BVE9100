#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>
#include <functional>

// Global log verbosity: 0 quiet, 1 info, 2 device traffic, 3 everything
extern int g_verbose;
extern uint64_t g_cycles;      // total CPU cycles executed (20 MHz 68030 clock)

static inline void logf(int level, const char* fmt, ...) {
    if (g_verbose < level) return;
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[%10llu] ", (unsigned long long)g_cycles);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}
#define LOGI(...) logf(1, __VA_ARGS__)
#define LOGD(...) logf(2, __VA_ARGS__)
#define LOGT(...) logf(3, __VA_ARGS__)

constexpr uint64_t CPU_HZ = 20000000;   // MC68030 @ 20 MHz (Sony brochure: "32-bit, 20 MHz")
