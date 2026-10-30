#include "usb_bridge.h"

#include "usb_lib.h"
#include "usb_mem.h"
#include "usb_prop.h"

static volatile uint8_t endpoint3_busy;

void EP1_IN_Callback(void)
{
}

void EP2_OUT_Callback(void)
{
    uint8_t buffer[64];
    const uint8_t length = (uint8_t)GetEPRxCount(ENDP2);

    PMAToUserBufferCopy(buffer, GetEPRxAddr(ENDP2), length);
    usb_bridge_rx_isr(buffer, length);
    SetEPRxValid(ENDP2);
}

void EP3_IN_Callback(void)
{
    endpoint3_busy = 0;
    usb_bridge_tx_complete_isr();
}

uint8_t USBD_ENDPx_DataUp(uint8_t endpoint, uint8_t *data, uint16_t length)
{
    if (endpoint != ENDP3 || endpoint3_busy || length > 64) {
        return USB_ERROR;
    }
    USB_SIL_Write(EP3_IN, data, length);
    endpoint3_busy = 1;
    SetEPTxStatus(ENDP3, EP_TX_VALID);
    return USB_SUCCESS;
}

void usb_endpoint_reset(void)
{
    endpoint3_busy = 0;
}
