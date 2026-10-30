#include "bridge.h"
#include "platform.h"
#include "usb_bridge.h"

#include "ch32v20x.h"

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    platform_init();
    for (unsigned pulse = 0; pulse < 3; ++pulse) {
        platform_led_set(1);
        platform_delay_ms(75);
        platform_led_set(0);
        platform_delay_ms(75);
    }
    bridge_init();
    usb_bridge_init();

    for (;;) {
        bridge_poll();
    }
}
