#ifndef ABN_USB_BRIDGE_H
#define ABN_USB_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

void usb_bridge_init(void);
int usb_bridge_configured(void);
int usb_bridge_read_byte(uint8_t *value);
int usb_bridge_send(const uint8_t *data, size_t length, uint32_t timeout_ms);
int usb_bridge_rx_overflowed(void);
void usb_bridge_rx_isr(const uint8_t *data, uint8_t length);
void usb_bridge_tx_complete_isr(void);

#endif
