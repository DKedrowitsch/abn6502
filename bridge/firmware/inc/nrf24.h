#ifndef ABN_NRF24_H
#define ABN_NRF24_H

#include <stdint.h>

enum nrf24_result {
    NRF24_TX_OK,
    NRF24_TX_MAX_RT,
    NRF24_TX_TIMEOUT
};

int nrf24_init(void);
enum nrf24_result nrf24_send(const uint8_t payload[32], uint32_t timeout_ms);
void nrf24_recover(void);
int nrf24_irq_active(void);

#endif
