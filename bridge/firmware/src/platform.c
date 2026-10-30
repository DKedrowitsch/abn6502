#include "platform.h"

#include "ch32v20x.h"

#define SYSTICK_HZ (96000000u / 8u)
#define TICKS_PER_US (SYSTICK_HZ / 1000000u)
#define TICKS_PER_MS (SYSTICK_HZ / 1000u)

void platform_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    gpio.GPIO_Pin = GPIO_Pin_2;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOB, &gpio);
    GPIO_ResetBits(GPIOB, GPIO_Pin_2);

    SysTick->CTLR = 0;
    SysTick->SR = 0;
    SysTick->CNT = 0;
    SysTick->CMP = UINT64_MAX;
    SysTick->CTLR = (1u << 5) | 1u;
}

static uint32_t platform_ticks(void)
{
    return (uint32_t)SysTick->CNT;
}

static uint64_t platform_ticks64(void)
{
    volatile const uint32_t *counter = (volatile const uint32_t *)&SysTick->CNT;
    uint32_t high_before;
    uint32_t high_after;
    uint32_t low;

    do {
        high_before = counter[1];
        low = counter[0];
        high_after = counter[1];
    } while (high_before != high_after);
    return ((uint64_t)high_after << 32) | low;
}

uint32_t platform_millis(void)
{
    return (uint32_t)(platform_ticks64() / TICKS_PER_MS);
}

void platform_delay_us(uint32_t microseconds)
{
    const uint32_t start = platform_ticks();
    const uint32_t ticks = microseconds * TICKS_PER_US;
    while ((uint32_t)(platform_ticks() - start) < ticks) {
    }
}

void platform_delay_ms(uint32_t milliseconds)
{
    while (milliseconds-- != 0u) {
        platform_delay_us(1000);
    }
}

void platform_led_set(int on)
{
    GPIO_WriteBit(GPIOB, GPIO_Pin_2, on ? Bit_SET : Bit_RESET);
}
