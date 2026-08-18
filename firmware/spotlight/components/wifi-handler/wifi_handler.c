#include "wifi_handler.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_event_base.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_types.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_wifi_types_generic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "led_driver.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define AP_SSID (CONFIG_SPOTLIGHT_AP_SSID)
#define AP_CHANNEL (CONFIG_SPOTLIGHT_AP_CHANNEL)
#define NVS_NAMESPACE "spotlight"

static const char *TAG = "WIFI_HANDLER";

/**
 * Attempts to retrieve the nvs config. On success returns true and on failure
 * returns false. False triggers the wifi Provisioning mode.
 */
bool wifi_handler_load_nvs_config(spotlight_config_t *config) {
  memset(config, 0, sizeof(spotlight_config_t));

  nvs_handle_t nvs_handle;
  esp_err_t success = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
  if (success == ESP_OK) {
    size_t size = sizeof(config->wifi_ssid);
    nvs_get_str(nvs_handle, "ssid", config->wifi_ssid, &size);
    size = sizeof(config->wifi_password);
    nvs_get_str(nvs_handle, "pass", config->wifi_password, &size);

    size = sizeof(config->mqtt_broker_uri);
    nvs_get_str(nvs_handle, "mqtt", config->mqtt_broker_uri, &size);

    size = sizeof(config->mqtt_username);
    nvs_get_str(nvs_handle, "mqtt_user", config->mqtt_username, &size);

    size = sizeof(config->mqtt_password);
    nvs_get_str(nvs_handle, "mqtt_pass", config->mqtt_password, &size);

    nvs_close(nvs_handle);
  } else {
    ESP_LOGW(TAG, "Failed to open NVS to load config (%s)",
             esp_err_to_name(success));
  }

  if (config->wifi_ssid[0] == '\0') {
    return false;
  }
  return true;
}
/**
 * Saves the provided wifi config in NVS.
 */
esp_err_t wifi_handler_save_nvs_config(const spotlight_config_t *config) {
  if (config->wifi_ssid[0] == '\0') {
    ESP_LOGE(TAG, "Cannot save config: SSID is empty");
    return ESP_FAIL;
  }

  nvs_handle_t nvs_handle;
  esp_err_t status = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (status != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS to save config (%s)",
             esp_err_to_name(status));
    return status;
  }

  ESP_ERROR_CHECK_WITHOUT_ABORT(
      nvs_set_str(nvs_handle, "ssid", config->wifi_ssid));
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      nvs_set_str(nvs_handle, "pass", config->wifi_password));
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      nvs_set_str(nvs_handle, "mqtt", config->mqtt_broker_uri));
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      nvs_set_str(nvs_handle, "mqtt_user", config->mqtt_username));
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      nvs_set_str(nvs_handle, "mqtt_pass", config->mqtt_password));

  esp_err_t commit_status = nvs_commit(nvs_handle);
  if (commit_status != ESP_OK) {
    ESP_LOGE(TAG, "Failed to commit NVS changes (%s)",
             esp_err_to_name(commit_status));
  }

  nvs_close(nvs_handle);
  return commit_status;
}

/**
 * Handles GET requests from the root uri. Sends and HTML form that retrieves
 * ssid, password, and an mqtt broker.
 *
 * TODO: Handle multiple brokers, broker security options, and associate
 * different messages to different lighting modes and priorities.
 *
 */
static esp_err_t root_get_handler(httpd_req_t *req) {
  const char *html_form =
      "<!DOCTYPE html><html><head><title>Spotlight Setup</title>"
      "<meta name='viewport' content='width=device-width, initial-scale=1'>"
      "<style>body{font-family:sans-serif;margin:20px;padding:10px;max-width:"
      "400px;}"
      "input{width:100%;padding:8px;margin:8px "
      "0;box-sizing:border-box;}</style></head><body>"
      "<h2>Spotlight Config Portal</h2>"
      "<form action='/save' method='POST'>"
      "<label>Wi-Fi SSID:</label><input type='text' name='ssid' required><br>"
      "<label>Wi-Fi Password:</label><input type='password' name='pass'><br>"
      "<label>MQTT Broker URI:</label><input type='text' name='mqtt' "
      "placeholder='mqtt://broker.hivemq.com'><br>"
      "<label>MQTT Username:</label><input type='text' name='mqtt_user'><br>"
      "<label>MQTT Password:</label><input type='password' "
      "name='mqtt_pass'><br>"
      "<input type='submit' value='Save Settings & Reboot' "
      "style='background:#007bff;color:#fff;border:none;padding:10px;cursor:"
      "pointer;'>"
      "</form></body></html>";

  httpd_resp_send(req, html_form, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

/**
 * Simple URL decoder helper to convert %XX hex escapes back to ASCII
 */
static void url_decode(char *str) {
    char *pstr = str, *buf = str;
    while (*pstr) {
        if (*pstr == '%') {
            if (pstr[1] && pstr[2]) {
                int hex_val;
                sscanf(pstr + 1, "%2x", &hex_val);
                *buf++ = (char)hex_val;
                pstr += 3;
            } else {
                *buf++ = *pstr++;
            }
        } else if (*pstr == '+') {
            *buf++ = ' ';
            pstr++;
        } else {
            *buf++ = *pstr++;
        }
    }
    *buf = '\0';
}

/**
 * Handles the POST request to the /save uri. Recieves the data as a & delimited
 * string in the form of: "ssid=___&password=___&mqtt=____\0"
 *
 * TODO: Handle the extra additions in root_get_handler, maybe partition data in
 * NVS by broker and store data as blobs where the message is the key?
 */
static esp_err_t save_post_handler(httpd_req_t *req) {
  char buf[512]; // Increased buffer size to accommodate extra fields
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0)
    return ESP_FAIL;
  buf[ret] = '\0'; // Null-terminate post body

  ESP_LOGI(TAG, "Received HTTP POST Data: %s", buf);

  spotlight_config_t config;
  memset(&config, 0, sizeof(spotlight_config_t));

  if (httpd_query_key_value(buf, "ssid", config.wifi_ssid,
                            sizeof(config.wifi_ssid)) != ESP_OK) {
    ESP_LOGE(TAG, "SSID field missing!"); // Necessary field
    return ESP_FAIL;
  }
  url_decode(config.wifi_ssid);

  if (httpd_query_key_value(buf, "pass", config.wifi_password,
                            sizeof(config.wifi_password)) == ESP_OK) {
      url_decode(config.wifi_password);
  }

  if (httpd_query_key_value(buf, "mqtt", config.mqtt_broker_uri,
                            sizeof(config.mqtt_broker_uri)) != ESP_OK) {
    ESP_LOGW(TAG, "MQTT URI field missing"); // Necessary field
  } else {
      url_decode(config.mqtt_broker_uri);
  }

  if (httpd_query_key_value(buf, "mqtt_user", config.mqtt_username,
                            sizeof(config.mqtt_username)) == ESP_OK) {
      url_decode(config.mqtt_username);
  }
  
  if (httpd_query_key_value(buf, "mqtt_pass", config.mqtt_password,
                            sizeof(config.mqtt_password)) == ESP_OK) {
      url_decode(config.mqtt_password);
  }

  wifi_handler_save_nvs_config(&config);

  ESP_LOGI(TAG, "Saved config, Rebooting the device");

  httpd_resp_send(req, "<h2>Configuration Saved! Rebooting device...</h2>",
                  HTTPD_RESP_USE_STRLEN);
  vTaskDelay(pdMS_TO_TICKS(2000));
  esp_restart();

  return ESP_OK;
}

/**
 * Wifi Provisioning mode, starts a local wifi network, pulses dim light to
 * indicate, access by joining wifi network and querying the root uri.
 */
void wifi_handler_start_softap_provisioning(void) {

  ESP_LOGI(TAG, "Starting SoftAP Mode: Network Name = '%s'", AP_SSID);

  esp_wifi_set_mode(WIFI_MODE_AP);
  wifi_config_t ap_config = {.ap.ssid = AP_SSID,
                             .ap.password = "",
                             .ap.channel = AP_CHANNEL,
                             .ap.authmode = WIFI_AUTH_OPEN,
                             .ap.max_connection = 4};
  esp_wifi_set_config(WIFI_IF_AP, &ap_config);

  esp_wifi_start();

  httpd_handle_t wifi_handle;
  httpd_config_t wifi_config = HTTPD_DEFAULT_CONFIG();

  if (httpd_start(&wifi_handle, &wifi_config) == ESP_OK) {
    led_cmd_t pulse_cmd = {.action = LED_ACTION_PULSE,
                           .priority = PRIORITY_MEDIUM,
                           .duty = 256,
                           .period_ms = 1000};

    led_driver_send_cmd(&pulse_cmd);

    httpd_uri_t root_get = {.uri = "/",
                            .method = HTTP_GET,
                            .handler = root_get_handler,
                            .user_ctx = NULL};
    httpd_uri_t save_post = {.uri = "/save",
                             .method = HTTP_POST,
                             .handler = save_post_handler,
                             .user_ctx = NULL};
    httpd_register_uri_handler(wifi_handle, &root_get);
    httpd_register_uri_handler(wifi_handle, &save_post);
  }
}

/**
 * Connect to a wifi network in station mode using the wifi config that should
 * be within NVS.
 */
void wifi_handler_start_sta(const spotlight_config_t *config) {
  ESP_LOGI(TAG, "Connecting to Wi-Fi SSID: '%s'...", config->wifi_ssid);
  wifi_config_t sta_config = {
      .sta.threshold.authmode = (strlen(config->wifi_password) == 0)
                                    ? WIFI_AUTH_OPEN
                                    : WIFI_AUTH_WPA2_PSK,
      .sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
      .sta.failure_retry_cnt = 5,
      .sta.scan_method = WIFI_ALL_CHANNEL_SCAN,
      .sta.pmf_cfg = {.capable = true, .required = false}};
  strncpy((char *)sta_config.sta.ssid, config->wifi_ssid,
          sizeof(sta_config.sta.ssid));
  strncpy((char *)sta_config.sta.password, config->wifi_password,
          sizeof(sta_config.sta.password));
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_set_config(WIFI_IF_STA, &sta_config);
  esp_wifi_start();

  led_cmd_t led_connecting = {.action = LED_ACTION_PULSE,
                              .duty = 256,
                              .period_ms = 1000,
                              .priority = PRIORITY_OVERRIDE};
  led_driver_send_cmd(&led_connecting);

  ESP_LOGI(TAG, "Successfully connected to Wi-Fi SSID: '%s'",
           config->wifi_ssid);
  return;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGW(TAG, "Lost connection to AP, Retrying...");
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
    led_cmd_t led_connected = {.action = LED_ACTION_FLASH,
                               .duty = 256,
                               .period_ms = 750,
                               .priority = PRIORITY_OVERRIDE};
    led_driver_send_cmd(&led_connected);

    // Ready to connect to MQTT!
  }
}
/**
 * Main Entry Point
 */
void wifi_handler_init(void) {
  // Initialize NVS Flash Partition
  esp_err_t ret = nvs_flash_init();           // Initialize Flash
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||     //
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) { // Flash Error encountered, Erase
                                              // and re-initalize.
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Initialize TCP/IP Stack & Event Loop
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_ap();
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(
      esp_event_handler_instance_register( // Handle WiFi connecting,
                                           // disconnecting, and success in
                                           // obtaining an IP
          WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

  // Try loading config from NVS
  spotlight_config_t config;
  if (wifi_handler_load_nvs_config(&config)) {
    wifi_handler_start_sta(&config);
  } else { // Issue with NVS or configuration never loaded: enter Provisioning
           // mode.
    ESP_LOGW(TAG,
             "No valid Wi-Fi credentials in NVS! Launching Provisioning...");
    wifi_handler_start_softap_provisioning();
  }
}
