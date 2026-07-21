#include <stdio.h>
#include "led_driver.h"

void app_main(void)
{
    led_driver_init();
    while (1) {
       start_fading();
    } 
}
