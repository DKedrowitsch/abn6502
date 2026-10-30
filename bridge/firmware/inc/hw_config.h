#ifndef __HW_CONFIG_H
#define __HW_CONFIG_H

#include "usb_type.h"

void Set_USBConfig(void);
void Enter_LowPowerMode(void);
void Leave_LowPowerMode(void);
void USB_Interrupts_Config(void);
void USB_Port_Set(FunctionalState state, FunctionalState pullup);

#endif
