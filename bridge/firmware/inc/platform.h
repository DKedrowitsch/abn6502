#ifndef ABN_PLATFORM_H
#define ABN_PLATFORM_H

#include <stdint.h>

void platform_init(void);
uint32_t platform_millis(void);
void platform_delay_us(uint32_t microseconds);
void platform_delay_ms(uint32_t milliseconds);
void platform_led_set(int on);

#endif
