#include "usart8251.h"
void Usart8251::reset() { phase_ = 0; mode_ = cmd_ = 0; rx_full_ = false; }
void Usart8251::rx_byte(uint8_t b) { rx_ = b; rx_full_ = true; if (on_rxrdy && (cmd_ & 0x04)) on_rxrdy(true); }
uint8_t Usart8251::read8(uint32_t off) {
    if ((off & 1) == 0) { rx_full_ = false; if (on_rxrdy) on_rxrdy(false); return rx_; }
    // status: TxRDY | RxRDY | TxEMPTY | DSR
    return (uint8_t)(0x01 | (rx_full_ ? 0x02 : 0) | 0x04 | 0x80);
}
void Usart8251::write8(uint32_t off, uint8_t v) {
    if ((off & 1) == 0) { if (on_tx) on_tx(v); return; }
    if (phase_ == 0) { mode_ = v; phase_ = 1; LOGD("%s: mode %02x\n", nm_, v); return; }
    if (v & 0x40) { phase_ = 0; LOGD("%s: internal reset\n", nm_); return; }
    cmd_ = v; LOGT("%s: command %02x\n", nm_, v);
}
