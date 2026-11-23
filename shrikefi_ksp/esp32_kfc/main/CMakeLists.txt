idf_component_register(
    SRCS "display.c" "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        esp_driver_gpio
        driver
        esp_driver_i2c
)