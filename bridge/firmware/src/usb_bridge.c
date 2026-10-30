#include "usb_bridge.h"

#include "platform.h"

#include "usb_lib.h"
#include "usb_prop.h"
#include "usb_pwr.h"

#define RX_RING_SIZE 128u

static volatile uint8_t rx_ring[RX_RING_SIZE];
static volatile uint8_t rx_head;
static volatile uint8_t rx_tail;
static volatile uint8_t rx_overflow;
static volatile uint8_t tx_complete;

void usb_bridge_init(void)
{
    Set_USBConfig();
    USB_Init();
    USB_Interrupts_Config();
}

int usb_bridge_configured(void)
{
    return bDeviceState == CONFIGURED;
}

void usb_bridge_rx_isr(const uint8_t *data, uint8_t length)
{
    while (length-- != 0u) {
        const uint8_t next = (uint8_t)((rx_head + 1u) & (RX_RING_SIZE - 1u));
        if (next == rx_tail) {
            rx_overflow = 1;
            return;
        }
        rx_ring[rx_head] = *data++;
        rx_head = next;
    }
}

void usb_bridge_tx_complete_isr(void)
{
    tx_complete = 1;
}

int usb_bridge_read_byte(uint8_t *value)
{
    if (rx_head == rx_tail) {
        return 0;
    }
    *value = rx_ring[rx_tail];
    rx_tail = (uint8_t)((rx_tail + 1u) & (RX_RING_SIZE - 1u));
    return 1;
}

int usb_bridge_send(const uint8_t *data, size_t length, uint32_t timeout_ms)
{
    const uint32_t start = platform_millis();

    if (!usb_bridge_configured() || length > 64) {
        return 0;
    }
    tx_complete = 0;
    while (USBD_ENDPx_DataUp(ENDP3, (uint8_t *)data, (uint16_t)length) != USB_SUCCESS) {
        if (!usb_bridge_configured()
            || (uint32_t)(platform_millis() - start) >= timeout_ms) {
            return 0;
        }
    }
    while (!tx_complete) {
        if (!usb_bridge_configured()
            || (uint32_t)(platform_millis() - start) >= timeout_ms) {
            return 0;
        }
    }
    return 1;
}

int usb_bridge_rx_overflowed(void)
{
    const int overflowed = rx_overflow;
    rx_overflow = 0;
    return overflowed;
}
