#include "mqtt_handler.h"
#include "esp_log.h"
#include "led_driver.h"
#include "mqtt_client.h"
#include "sdkconfig.h"
#include <string.h>

#define MQTT_TEST_TOPIC (CONFIG_SPOTLIGHT_MQTT_TEST_TOPIC)

static const char *TAG = "MQTT_HANDLER";
static esp_mqtt_client_handle_t client = NULL;

/**
 * Checks for all MQTT events passed into the event handler, checks the event
 * data and sends a corresponding LED command.
 *
 * TODO: Build a Dynamic Reaction Engine using NVS Blobs.
 *       Currently, string payloads like "FLASH" are hardcoded to LED actions.
 *       We need to store customizable Message-to-Action mappings as binary blobs 
 *       in NVS, so users can map arbitrary strings (like "ALERT_FRONT_DOOR") 
 *       to any LED mode dynamically.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = event_data;

  switch ((esp_mqtt_event_id_t)event_id) {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
    esp_mqtt_client_subscribe(client, MQTT_TEST_TOPIC, 2);
    break;

  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
    break;

  case MQTT_EVENT_SUBSCRIBED:
    ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
    break;

  case MQTT_EVENT_DATA:
    ESP_LOGI(TAG, "MQTT_EVENT_DATA");
    ESP_LOGI(TAG, "TOPIC=%.*s", event->topic_len, event->topic);
    ESP_LOGI(TAG, "DATA=%.*s", event->data_len, event->data);

    // Create a base command with default parameters
    led_cmd_t cmd = {
        .priority = PRIORITY_MEDIUM, .duty = 1023, .period_ms = 1000};

    // CRITICAL: event->data is NOT null-terminated!
    // We check both the length and the string contents using strncmp.
    if (event->data_len == 3 && strncmp(event->data, "OFF", 3) == 0) {
      cmd.action = LED_ACTION_OFF;
      led_driver_send_cmd(&cmd);
    } else if (event->data_len == 2 && strncmp(event->data, "ON", 2) == 0) {
      cmd.action = LED_ACTION_ON;
      led_driver_send_cmd(&cmd);
    } else if (event->data_len == 8 &&
               strncmp(event->data, "SET_DUTY", 8) == 0) {
      cmd.action = LED_ACTION_SET_DUTY;
      cmd.duty = 512; // Example: Set to 50% brightness
      led_driver_send_cmd(&cmd);
    } else if (event->data_len == 5 && strncmp(event->data, "PULSE", 5) == 0) {
      cmd.action = LED_ACTION_PULSE;
      cmd.period_ms = 2000; // 2 second breathing cycle
      led_driver_send_cmd(&cmd);
    } else if (event->data_len == 5 && strncmp(event->data, "FLASH", 5) == 0) {
      cmd.action = LED_ACTION_FLASH;
      cmd.period_ms = 500; // 500ms flash cycle
      led_driver_send_cmd(&cmd);
    } else {
      ESP_LOGW(TAG, "Unknown command received: %.*s", event->data_len,
               event->data);
    }
    break;

  case MQTT_EVENT_ERROR:
    ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
    break;

  default:
    ESP_LOGI(TAG, "Other event id:%d", event->event_id);
    break;
  }
}
// TODO: Make this support multiple brokers and multiple topics, each broker
// with its own credentials or verification.
esp_err_t mqtt_handler_init(const spotlight_config_t *config) {
  ESP_LOGI(TAG, "Initializing MQTT handler, broker: %s",
           config->mqtt_broker_uri);

  esp_mqtt_client_config_t mqtt_config = {.broker.address.uri =
                                              config->mqtt_broker_uri};

  if (config->mqtt_username[0] != '\0')
    mqtt_config.credentials.username = config->mqtt_username;
  if (config->mqtt_password[0] != '\0')
    mqtt_config.credentials.authentication.password = config->mqtt_password;

  client = esp_mqtt_client_init(&mqtt_config);

  ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, MQTT_EVENT_ANY,
                                                 mqtt_event_handler, NULL));
  ESP_ERROR_CHECK(esp_mqtt_client_start(client));
  return ESP_OK;
}
