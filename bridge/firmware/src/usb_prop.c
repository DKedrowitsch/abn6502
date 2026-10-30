#include "usb_lib.h"
#include "usb_conf.h"
#include "usb_desc.h"
#include "usb_pwr.h"
#include "usb_prop.h"
#include "hw_config.h"
#include "platform.h"

extern void usb_endpoint_reset(void);

static uint8_t line_coding[7] = {0x00, 0xc2, 0x01, 0x00, 0x00, 0x00, 0x08};

DEVICE Device_Table = {EP_NUM, 1};

static uint8_t *get_line_coding(uint16_t length);
static uint8_t *set_line_coding(uint16_t length);

DEVICE_PROP Device_Property = {
    USBD_init, USBD_Reset, USBD_Status_In, USBD_Status_Out,
    USBD_Data_Setup, USBD_NoData_Setup, USBD_Get_Interface_Setting,
    USBD_GetDeviceDescriptor, USBD_GetConfigDescriptor,
    USBD_GetStringDescriptor, 0, DEF_USBD_UEP0_SIZE
};

USER_STANDARD_REQUESTS User_Standard_Requests = {
    NOP_Process, USBD_SetConfiguration, NOP_Process, NOP_Process,
    NOP_Process, USBD_ClearFeature, NOP_Process,
    USBD_SetDeviceFeature, USBD_SetDeviceAddress
};

ONE_DESCRIPTOR Device_Descriptor = {(uint8_t *)USBD_DeviceDescriptor, USBD_SIZE_DEVICE_DESC};
ONE_DESCRIPTOR Config_Descriptor = {(uint8_t *)USBD_ConfigDescriptor, USBD_SIZE_CONFIG_DESC};
ONE_DESCRIPTOR String_Descriptor[4] = {
    {(uint8_t *)USBD_StringLangID, USBD_SIZE_STRING_LANGID},
    {(uint8_t *)USBD_StringVendor, USBD_SIZE_STRING_VENDOR},
    {(uint8_t *)USBD_StringProduct, USBD_SIZE_STRING_PRODUCT},
    {(uint8_t *)USBD_StringSerial, USBD_SIZE_STRING_SERIAL}
};

void USBD_SetConfiguration(void)
{
    if (Device_Info.Current_Configuration != 0) bDeviceState = CONFIGURED;
}

void USBD_SetDeviceAddress(void)
{
    bDeviceState = ADDRESSED;
}

void USBD_SetDeviceFeature(void) {}
void USBD_ClearFeature(void) {}
void USBD_Status_In(void) {}
void USBD_Status_Out(void) {}

void USBD_init(void)
{
    pInformation->Current_Configuration = 0;
    PowerOn();
    for (uint8_t endpoint = 0; endpoint < 8; ++endpoint) {
        _SetENDPOINT(endpoint, _GetENDPOINT(endpoint) & 0x7f7f & EPREG_MASK);
    }
    _SetISTR(0x00ff);
    USB_SIL_Init();
    bDeviceState = UNCONNECTED;
    USB_Port_Set(DISABLE, DISABLE);
    platform_delay_ms(20);
    USB_Port_Set(ENABLE, ENABLE);
}

void USBD_Reset(void)
{
    pInformation->Current_Configuration = 0;
    pInformation->Current_Feature = USBD_ConfigDescriptor[7];
    pInformation->Current_Interface = 0;
    SetBTABLE(BTABLE_ADDRESS);

    SetEPType(ENDP0, EP_CONTROL);
    SetEPTxStatus(ENDP0, EP_TX_STALL);
    SetEPRxAddr(ENDP0, ENDP0_RXADDR);
    SetEPTxAddr(ENDP0, ENDP0_TXADDR);
    Clear_Status_Out(ENDP0);
    SetEPRxCount(ENDP0, Device_Property.MaxPacketSize);
    SetEPRxValid(ENDP0);
    _ClearDTOG_RX(ENDP0);
    _ClearDTOG_TX(ENDP0);

    SetEPType(ENDP1, EP_INTERRUPT);
    SetEPTxStatus(ENDP1, EP_TX_NAK);
    SetEPTxAddr(ENDP1, ENDP1_TXADDR);
    SetEPRxStatus(ENDP1, EP_RX_DIS);
    _ClearDTOG_TX(ENDP1);
    _ClearDTOG_RX(ENDP1);

    SetEPType(ENDP2, EP_BULK);
    SetEPTxStatus(ENDP2, EP_TX_DIS);
    SetEPRxAddr(ENDP2, ENDP2_RXADDR);
    SetEPRxCount(ENDP2, DEF_USBD_MAX_PACK_SIZE);
    SetEPRxValid(ENDP2);
    _ClearDTOG_RX(ENDP2);
    _ClearDTOG_TX(ENDP2);

    SetEPType(ENDP3, EP_BULK);
    SetEPTxStatus(ENDP3, EP_TX_NAK);
    SetEPTxAddr(ENDP3, ENDP3_TXADDR);
    SetEPRxStatus(ENDP3, EP_RX_DIS);
    _ClearDTOG_TX(ENDP3);
    _ClearDTOG_RX(ENDP3);

    SetDeviceAddress(0);
    usb_endpoint_reset();
    bDeviceState = ATTACHED;
}

uint8_t *USBD_GetDeviceDescriptor(uint16_t length)
{
    return Standard_GetDescriptorData(length, &Device_Descriptor);
}

uint8_t *USBD_GetConfigDescriptor(uint16_t length)
{
    return Standard_GetDescriptorData(length, &Config_Descriptor);
}

uint8_t *USBD_GetStringDescriptor(uint16_t length)
{
    const uint8_t index = pInformation->USBwValue0;
    return index > 3 ? 0 : Standard_GetDescriptorData(length, &String_Descriptor[index]);
}

RESULT USBD_Get_Interface_Setting(uint8_t interface, uint8_t alternate)
{
    return alternate > 0 || interface > 1 ? USB_UNSUPPORT : USB_SUCCESS;
}

static uint8_t *get_line_coding(uint16_t length)
{
    if (length == 0) {
        pInformation->Ctrl_Info.Usb_wLength = sizeof(line_coding);
        return 0;
    }
    return line_coding;
}

static uint8_t *set_line_coding(uint16_t length)
{
    return get_line_coding(length);
}

RESULT USBD_Data_Setup(uint8_t request_number)
{
    uint8_t *(*copy)(uint16_t) = 0;
    if (Type_Recipient != (CLASS_REQUEST | INTERFACE_RECIPIENT)) return USB_UNSUPPORT;
    if (pInformation->USBwIndex != 0 || pInformation->USBwValue != 0
        || pInformation->USBwLength != sizeof(line_coding)) return USB_UNSUPPORT;
    if (request_number == CDC_GET_LINE_CODING
        && pInformation->USBbmRequestType == 0xa1) copy = get_line_coding;
    if (request_number == CDC_SET_LINE_CODING
        && pInformation->USBbmRequestType == 0x21) copy = set_line_coding;
    if (copy == 0) return USB_UNSUPPORT;
    pInformation->Ctrl_Info.CopyData = copy;
    pInformation->Ctrl_Info.Usb_wOffset = 0;
    copy(0);
    return USB_SUCCESS;
}

RESULT USBD_NoData_Setup(uint8_t request_number)
{
    if (Type_Recipient != (CLASS_REQUEST | INTERFACE_RECIPIENT)) return USB_UNSUPPORT;
    if (pInformation->USBbmRequestType != 0x21 || pInformation->USBwIndex != 0
        || pInformation->USBwLength != 0) return USB_UNSUPPORT;
    return request_number == CDC_SET_LINE_CTLSTE || request_number == CDC_SEND_BREAK
        ? USB_SUCCESS : USB_UNSUPPORT;
}
