#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/ledc.h"

// The commands that can be sent to the component over the queue.
typedef enum {
    LED_CMD_STOP,
    LED_CMD_START_PULSE
} led_command_t;

// Expose the queue handle globally so other components can post to it
extern QueueHandle_t xLedQueue;

/**
 * @brief Initializes the LEDC peripheral and spawns the FreeRTOS LED task.
 */
void led_driver_init(void);
void start_fading(void);

#endif // LED_DRIVER_H