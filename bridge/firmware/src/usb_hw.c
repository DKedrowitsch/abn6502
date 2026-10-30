#include "hw_config.h"

#include "usb_istr.h"
#include "usb_lib.h"
#include "usb_pwr.h"

#if defined(__riscv_xw)
#define ABN_INTERRUPT __attribute__((interrupt("WCH-Interrupt-fast")))
#else
#define ABN_INTERRUPT __attribute__((interrupt("machine")))
#endif

void USBWakeUp_IRQHandler(void) ABN_INTERRUPT;
void USB_LP_CAN1_RX0_IRQHandler(void) ABN_INTERRUPT;

void USBWakeUp_IRQHandler(void)
{
    EXTI_ClearITPendingBit(EXTI_Line18);
}

void USB_LP_CAN1_RX0_IRQHandler(void)
{
    USB_Istr();
}

void Set_USBConfig(void)
{
    RCC_ClocksTypeDef clocks = {0};
    RCC_GetClocksFreq(&clocks);
    if (clocks.SYSCLK_Frequency == 96000000u) {
        RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div2);
    } else if (clocks.SYSCLK_Frequency == 144000000u) {
        RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div3);
    } else {
        RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div1);
    }
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USB, ENABLE);
}

void Enter_LowPowerMode(void)
{
    bDeviceState = SUSPENDED;
}

void Leave_LowPowerMode(void)
{
    bDeviceState = Device_Info.Current_Configuration != 0 ? CONFIGURED : ATTACHED;
}

void USB_Interrupts_Config(void)
{
    EXTI_InitTypeDef exti = {0};
    NVIC_InitTypeDef nvic = {0};

    EXTI_ClearITPendingBit(EXTI_Line18);
    exti.EXTI_Line = EXTI_Line18;
    exti.EXTI_Mode = EXTI_Mode_Interrupt;
    exti.EXTI_Trigger = EXTI_Trigger_Rising_Falling;
    exti.EXTI_LineCmd = ENABLE;
    EXTI_Init(&exti);

    nvic.NVIC_IRQChannel = USB_LP_CAN1_RX0_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
    nvic.NVIC_IRQChannel = USBWakeUp_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_Init(&nvic);
}

void USB_Port_Set(FunctionalState state, FunctionalState pullup)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    if (state != DISABLE) {
        _SetCNTR(_GetCNTR() & ~(1u << 1));
        GPIOA->CFGHR &= 0xfff00fffu;
        GPIOA->OUTDR &= ~(3u << 11);
        GPIOA->CFGHR |= 0x00044000u;
    } else {
        _SetCNTR(_GetCNTR() | (1u << 1));
        GPIOA->CFGHR &= 0xfff00fffu;
        GPIOA->OUTDR &= ~(3u << 11);
        GPIOA->CFGHR |= 0x00033000u;
    }
    if (pullup != DISABLE) {
        EXTEN->EXTEN_CTR |= EXTEN_USBD_PU_EN;
    } else {
        EXTEN->EXTEN_CTR &= ~EXTEN_USBD_PU_EN;
    }
}
