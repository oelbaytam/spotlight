#ifndef WIFI_HANDLER_H
#define WIFI_HANDLER_H

#include <stdbool.h>
#include "esp_err.h"

// Configuration Structure stored in NVS
typedef struct {
    char wifi_ssid[32];
    char wifi_password[64];
    char mqtt_broker_uri[128];
    char mqtt_username[64];
    char mqtt_password[64];
} spotlight_config_t;

/**
 * Reads stored credentials from NVS. Returns true if valid credentials exist.
 */
bool wifi_handler_load_nvs_config(spotlight_config_t *config);

/**
 * Saves new credentials to NVS.
 */
esp_err_t wifi_handler_save_nvs_config(const spotlight_config_t *config);

/**
 * Starts Wi-Fi in SoftAP mode and launches the HTTP Web Server for provisioning.
 */
void wifi_handler_start_softap_provisioning(void);

/**
 * Starts Wi-Fi in Station Mode using credentials loaded from NVS.
 */
void wifi_handler_start_sta(const spotlight_config_t *config);

/**
 * Main initialization entry point.
 */
void wifi_handler_init(void);

#endif // WIFI_HANDLER_H
