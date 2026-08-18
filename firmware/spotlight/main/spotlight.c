#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "led_driver.h"
#include "button_handler.h"
#include "wifi_handler.h"
#include "mqtt_handler.h"

static const char *TAG = "MAIN_APP";

// Callback function invoked when 5s Long Press is detected on the button
void on_provisioning_long_press(void) {
    ESP_LOGW(TAG, "=====================================================");
    ESP_LOGW(TAG, "PROVISIONING TRIGGERED! (5s Long Hold Recognized)");
    ESP_LOGW(TAG, "Erasing NVS config and rebooting to SoftAP mode...");
    ESP_LOGW(TAG, "=====================================================");
    nvs_flash_erase();
    esp_restart();
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing Spotlight Firmware...");
    
    // 1. Initialize LED Driver
    led_driver_init();

    // 2. Initialize Button Handler with Long Press Callback
    button_handler_init(on_provisioning_long_press);

    // 3. Initialize Wi-Fi subsystem (this automatically handles NVS and starts STA or SoftAP)
    wifi_handler_init();

    // 4. If we have valid Wi-Fi credentials, initialize the MQTT Reaction Engine!
    spotlight_config_t config;
    if (wifi_handler_load_nvs_config(&config)) {
        ESP_LOGI(TAG, "Valid config found, starting Reaction Engine...");
        mqtt_handler_init(&config);
    } else {
        ESP_LOGI(TAG, "No config found, skipping MQTT initialization. Awaiting Provisioning...");
    }

    ESP_LOGI(TAG, "-----------------------------------------------------");
    ESP_LOGI(TAG, "SYSTEM READY:");
    ESP_LOGI(TAG, "1. Short Press (< 1.5s): Should immediately turn OFF LEDs.");
    ESP_LOGI(TAG, "2. Long Press (>= 5s): Erase config and trigger PROVISIONING.");
    ESP_LOGI(TAG, "3. Connect to Wi-Fi to send MQTT Commands!");
    ESP_LOGI(TAG, "-----------------------------------------------------");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
