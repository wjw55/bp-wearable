idf_component_register(
    SRCS
        "app_main.c"
        "max30102.c"
        "signal_processing.c"
        "wifi_http.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_event
        esp_http_client
        esp_netif
        esp_timer
        esp_wifi
        nvs_flash
)
