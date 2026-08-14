idf_component_register(
    SRCS "button_handler.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES freertos esp_timer led_driver esp_driver_gpio
)
