#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <stdbool.h>

/**
 * Callback function type invoked when a Long Press (5s hold) is detected.
 */
typedef void (*button_long_press_cb_t)(void);

/**
 * Initializes the button GPIO pin and spawns the gesture detection task.
 * @param long_press_cb Callback function to invoke when provisioning hold threshold is met.
 */
void button_handler_init(button_long_press_cb_t long_press_cb);

#endif // BUTTON_HANDLER_H
