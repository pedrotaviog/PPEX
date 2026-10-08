idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_event driver esp_timer bt nvs_flash esp_adc esp_driver_ledc esp_driver_pcnt)