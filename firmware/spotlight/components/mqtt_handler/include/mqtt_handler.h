#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include "esp_err.h"
#include "wifi_handler.h"

/**
 * @brief Initialize the MQTT client and connect to the broker.
 * 
 * @param config The spotlight configuration containing URI and credentials
 * @return esp_err_t ESP_OK on success, or an error code
 */
esp_err_t mqtt_handler_init(const spotlight_config_t *config);

#endif // MQTT_HANDLER_H
