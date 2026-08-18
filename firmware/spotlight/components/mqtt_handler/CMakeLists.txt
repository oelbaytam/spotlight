idf_component_register(SRCS "mqtt_handler.c"
                       INCLUDE_DIRS "include"
                       REQUIRES mqtt led_driver wifi-handler)
