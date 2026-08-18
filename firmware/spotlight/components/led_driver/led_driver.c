#include "led_driver.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hal/ledc_types.h"
#include "portmacro.h"
#include "sdkconfig.h"

// Hardware Configuration from Kconfig / sdkconfig
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_TIMER ((ledc_timer_t)CONFIG_SPOTLIGHT_LEDC_TIMER)
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_GPIO (CONFIG_SPOTLIGHT_LED_GPIO)
#define LEDC_FREQ_HZ (CONFIG_SPOTLIGHT_PWM_FREQ_HZ)
#define LEDC_DUTY_RES LEDC_TIMER_10_BIT // 10-bit resolution (0..1023)
#define LEDC_MAX_DUTY                                                          \
  (2 << (LEDC_DUTY_RES - 1)) - 1 // 2**LEDC_DUTY_RES - 1 for max duty cycle
#define LEDC_SIGNAL_TIME_SEC 20  // Total signal duration in seconds
#define LEDC_SIGNAL_TIME_MS LEDC_SIGNAL_TIME_SEC * 1000

static const char *TAG = "LED_DRIVER";

// Global handle for the FreeRTOS Queue
QueueHandle_t xLedQueue = NULL;
static uint8_t active_priority = PRIORITY_LOW;

/**
 * Helper function to update LEDC duty cycle hardware.
 */
static void set_hardware_duty(uint32_t duty) {
  if (duty > LEDC_MAX_DUTY)
    duty = LEDC_MAX_DUTY;
  ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
  ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

/**
 * TODO: Implement Queue Overflow Protection.
 *       Ensure that led_driver_send_cmd gracefully handles scenarios where the 
 *       MQTT client gets flooded with commands (e.g. 1000 msgs/sec), preventing 
 *       the FreeRTOS queue from overflowing or dropping high priority alerts.
 */

/**
 * Takes an led_cmd_t type command and pushes it onto the LedQueue based on
 * priority.
 */
BaseType_t led_driver_send_cmd(const led_cmd_t *cmd) {
  if (xLedQueue == NULL) {
    ESP_LOGE(TAG, "Led command recieved with an uninitialized LedQueue");
    return pdFALSE;
  }
  led_cmd_t pending_cmd; // Temporary variable to hold the next command.
  bool queue_not_empty;
  queue_not_empty = (xQueuePeek(xLedQueue, &pending_cmd, 0) == pdTRUE);

  if (queue_not_empty && pending_cmd.priority <= cmd->priority) {
    xQueueSendToFront(xLedQueue, cmd,
                      pdMS_TO_TICKS(10)); // arbitrary 10ms timeout
  } else {
    xQueueSendToBack(xLedQueue, cmd, pdMS_TO_TICKS(10));
  }
  return pdTRUE;
}

/**
 * Returns true if next command in xLedQueue is a higher priority than the
 * currently running priority. Returns false otherwise.
 */
static bool check_for_preemption(void) {
  led_cmd_t pending_cmd;
  bool queue_not_empty;
  queue_not_empty = (xQueuePeek(xLedQueue, &pending_cmd, 0) == pdTRUE);
  if (queue_not_empty && pending_cmd.priority >= active_priority) {
    ESP_LOGI(TAG,
             "Overriding current command due to new higher priority command");
    return true;
  }
  return false;
}

/**
 * Delays for wait_ms while constantly checking for preempting commands.
 * By polling the queue every 20ms, we guarantee immediate responsiveness
 * without blocking the FreeRTOS scheduler.
 */
static bool wait_with_preemption(uint32_t wait_ms) {
  uint32_t elapsed = 0;
  uint32_t step = 20; // 20ms polling interval
  while (elapsed < wait_ms) {
    if (check_for_preemption()) {
      return true; // A new, higher/equal priority command has arrived
    }
    vTaskDelay(pdMS_TO_TICKS(step));
    elapsed += step;
  }
  return false;
}

/**
 * Main LED Driver Task
 */
static void led_task(void *pvParameters) {
  led_cmd_t current_cmd;

  while (1) {
    xQueueReceive(xLedQueue, &current_cmd, portMAX_DELAY);
    active_priority = current_cmd.priority;
    
    // Safety check: prevent divide-by-zero if period wasn't set
    uint32_t period = current_cmd.period_ms > 0 ? current_cmd.period_ms : 1000;

    switch (current_cmd.action) {

    case LED_ACTION_ON:
      ESP_LOGI(TAG, "Led turning on for %ds", LEDC_SIGNAL_TIME_SEC);
      set_hardware_duty(LEDC_MAX_DUTY);
      wait_with_preemption(LEDC_SIGNAL_TIME_MS);
      set_hardware_duty(0);
      break;

    case LED_ACTION_OFF:
      ESP_LOGI(TAG, "Led turning off");
      set_hardware_duty(0);
      break;

    case LED_ACTION_SET_DUTY:
      ESP_LOGI(TAG, "Led being set to %d for %ds", current_cmd.duty, LEDC_SIGNAL_TIME_SEC);
      set_hardware_duty(current_cmd.duty);
      wait_with_preemption(LEDC_SIGNAL_TIME_MS);
      set_hardware_duty(0);
      break;

    case LED_ACTION_FLASH:
      ESP_LOGI(TAG, "Flashing led for %ds at duty %d", LEDC_SIGNAL_TIME_SEC, current_cmd.duty);
      int flash_cycles = LEDC_SIGNAL_TIME_MS / period;
      for (int i = 0; i < flash_cycles; i++) {
        // ON Phase
        set_hardware_duty(current_cmd.duty);
        if (wait_with_preemption(period / 2)) break;
        
        // OFF Phase
        set_hardware_duty(0);
        if (wait_with_preemption(period / 2)) break;
      }
      set_hardware_duty(0);
      break;

    case LED_ACTION_PULSE:
      ESP_LOGI(TAG, "Pulsing led for %ds to duty %d", LEDC_SIGNAL_TIME_SEC, current_cmd.duty);
      int pulse_cycles = LEDC_SIGNAL_TIME_MS / period;
      for (int i = 0; i < pulse_cycles; i++) {
        // Fade UP
        ledc_set_fade_with_time(LEDC_MODE, LEDC_CHANNEL, current_cmd.duty, period / 2);
        ledc_fade_start(LEDC_MODE, LEDC_CHANNEL, LEDC_FADE_NO_WAIT);
        if (wait_with_preemption(period / 2)) break;

        // Fade DOWN
        ledc_set_fade_with_time(LEDC_MODE, LEDC_CHANNEL, 0, period / 2);
        ledc_fade_start(LEDC_MODE, LEDC_CHANNEL, LEDC_FADE_NO_WAIT);
        if (wait_with_preemption(period / 2)) break;
      }
      set_hardware_duty(0);
      break;
    }
    
    // Tiny sleep to allow context switching before reading next item
    vTaskDelay(pdMS_TO_TICKS(10));
    active_priority = PRIORITY_LOW;
  }
}

/**
 * Initializes the LEDC timers and fade handlers, and initializes the driver
 * task.
 */
void led_driver_init(void) {
  // Hardware Setup: LEDC Timer
  ledc_timer_config_t ledc_timer = {.duty_resolution = LEDC_DUTY_RES,
                                    .freq_hz = LEDC_FREQ_HZ,
                                    .speed_mode = LEDC_MODE,
                                    .timer_num = LEDC_TIMER,
                                    .clk_cfg = LEDC_AUTO_CLK};
  ledc_timer_config(&ledc_timer);

  // Hardware Setup: LEDC Channel
  ledc_channel_config_t ledc_channel = {.channel = LEDC_CHANNEL,
                                        .duty = 0,
                                        .gpio_num = LEDC_GPIO,
                                        .speed_mode = LEDC_MODE,
                                        .hpoint = 0,
                                        .timer_sel = LEDC_TIMER};
  ledc_channel_config(&ledc_channel);

  ledc_fade_func_install(0);

  xLedQueue = xQueueCreate(CONFIG_SPOTLIGHT_QUEUE_SIZE, sizeof(led_cmd_t));
  xTaskCreate(led_task, "led_task", 4096, NULL, 5, NULL);

  ESP_LOGI(TAG, "LED Driver hardware initialized on GPIO %d @ %d Hz", LEDC_GPIO,
           LEDC_FREQ_HZ);
}
