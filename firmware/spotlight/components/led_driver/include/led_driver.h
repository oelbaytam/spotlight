#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdbool.h>
#include <stdint.h>

// Priority Levels for Command Overriding
typedef enum {
  PRIORITY_LOW = 0,       // Ambient / Routine background status
  PRIORITY_MEDIUM = 1,    // Informational alert (e.g. standard pulse)
  PRIORITY_HIGH = 2,      // Emergency / High Priority flash
  PRIORITY_OVERRIDE = 255 // Manual Button Press (Immediate Hardware Off)
} led_priority_t;

// Supported LED Actions
typedef enum {
  LED_ACTION_OFF,      // Turn off LED (Duty = 0)
  LED_ACTION_ON,       // Turn on LED (Duty = Max 1023)
  LED_ACTION_SET_DUTY, // Set static duty cycle (0..1023)
  LED_ACTION_PULSE,    // Smooth breathing/fade effect over period_ms
  LED_ACTION_FLASH     // Square wave on/off blink over period_ms
} led_action_t;

// Complete Command Structure sent over the FreeRTOS Queue
typedef struct {
  led_action_t action;     // Target action
  led_priority_t priority; // Message priority level
  uint16_t duty;           // Target duty (0..1023) for SET_DUTY or PULSE peak
  uint32_t period_ms; // Full cycle period in milliseconds (for PULSE/FLASH)
} led_cmd_t;

// Global Queue Handle
extern QueueHandle_t xLedQueue;

/**
 * @brief Thread-safe helper to send commands to the LED queue based on
 * priority.
 */
BaseType_t led_driver_send_cmd(const led_cmd_t *cmd);

/**
 * @brief Initializes LEDC peripheral, creates the queue, and spawns the LED
 * task.
 */
void led_driver_init(void);

#endif // LED_DRIVER_H
