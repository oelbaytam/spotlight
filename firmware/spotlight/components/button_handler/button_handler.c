#include "button_handler.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "led_driver.h"
#include "sdkconfig.h"
#include <stdio.h>

#define BUTTON_GPIO (CONFIG_SPOTLIGHT_BUTTON_GPIO)
#define LONG_PRESS_HOLD_MS (CONFIG_SPOTLIGHT_BUTTON_LONG_PRESS_MS)

static const char *TAG = "BUTTON_HANDLER";
static button_long_press_cb_t s_long_press_cb = NULL;

/**
 * Button handler task
 * Checks for a pulldown on the button pin, records time.
 * if time > 100ms and time < LONG_PRESS_HOLD_MS:
 *    a stop override is sent to the LED driver
 * if time > LONG_PRESS_HOLD_MS:
 *    a callback function is called that handles provisioning
 * if time < 100ms:
 *    it is considered a false input and nothing happens.
 */
static void button_task(void *pvParameters) {

  while (1) {
    if (gpio_get_level(BUTTON_GPIO) == 0) {
      int start_ticks = xTaskGetTickCount();
      int elapsed_ms = 0;
      while (gpio_get_level(BUTTON_GPIO) == 0) {
        ESP_LOGI(TAG, "Active Press, current duration = %d",
                 pdTICKS_TO_MS(xTaskGetTickCount() - start_ticks));
        vTaskDelay(pdMS_TO_TICKS(20));
      }
      elapsed_ms = pdTICKS_TO_MS(xTaskGetTickCount() - start_ticks);
      if (elapsed_ms >= LONG_PRESS_HOLD_MS) {
        ESP_LOGI(TAG, "Long press detected! Triggering provisioning");
        if (s_long_press_cb != NULL) {
          s_long_press_cb();
        }
      } else if (elapsed_ms >= 80) {
        ESP_LOGI(TAG, "Short press detected!");
        led_cmd_t command = {LED_ACTION_OFF, PRIORITY_OVERRIDE, 0, 0};
        led_driver_send_cmd(&command);
      } else {
        ESP_LOGI(TAG, "A %d ms press was ignored for being under 80ms",
                 elapsed_ms);
      }
    }
    // Dummy delay so task doesn't trigger Watchdog timer
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

/**
 * Initializes the task, gpio pin, and long press callback function.
 */
void button_handler_init(button_long_press_cb_t long_press_cb) {

  gpio_config_t io_conf = {(1ULL << BUTTON_GPIO), GPIO_MODE_INPUT,
                           GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE,
                           GPIO_INTR_DISABLE};

  gpio_config(&io_conf);
  s_long_press_cb = long_press_cb;
  xTaskCreate(button_task, "Button Handler Task", 4096, NULL, 5, NULL);

  ESP_LOGI(TAG, "Button Handler initialized on GPIO %d (Long Hold = %d ms)",
           BUTTON_GPIO, LONG_PRESS_HOLD_MS);
}
