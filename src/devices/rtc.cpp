#include "rtc.h"
#include <ctime>
uint8_t Rtc::read8(uint32_t off) {
    int r = off & 0xF;
    if (r >= 0xD) return ctl_[r - 0xD] & 0xF;
    time_t t = time(nullptr); struct tm* tm = localtime(&t);
    int yy = tm->tm_year % 100;
    switch (r) {
    case 0: return tm->tm_sec % 10;  case 1: return tm->tm_sec / 10;
    case 2: return tm->tm_min % 10;  case 3: return tm->tm_min / 10;
    case 4: return tm->tm_hour % 10; case 5: return (tm->tm_hour / 10) | ((ctl_[2] & 4) ? 0 : 0);
    case 6: return tm->tm_wday;
    case 7: return tm->tm_mday % 10; case 8: return tm->tm_mday / 10;
    case 9: return (tm->tm_mon + 1) % 10; case 0xA: return (tm->tm_mon + 1) / 10;
    case 0xB: return yy % 10; case 0xC: return yy / 10;
    }
    return 0;
}
void Rtc::write8(uint32_t off, uint8_t v) {
    int r = off & 0xF;
    if (r >= 0xD) ctl_[r - 0xD] = v & 0xF;
    else LOGD("RTC: write reg %x = %x (ignored, host clock is authoritative)\n", r, v & 0xF);
}
