
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"

#define BLINK_GPIO GPIO_NUM_4

static const char *TAG = "BLINK";

void app_main(void)
{
    // Configure the GPIO pin as an output
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BLINK_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    // Apply the GPIO configuration
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    ESP_LOGI(TAG, "Blinky program started!");

    while (1)
    {
        // Turn LED ON
        ESP_ERROR_CHECK(gpio_set_level(BLINK_GPIO, 1));
        ESP_LOGI(TAG, "LED ON");

        // Wait for one second
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Turn LED OFF
        ESP_ERROR_CHECK(gpio_set_level(BLINK_GPIO, 0));
        ESP_LOGI(TAG, "LED OFF");

        // Wait for one second
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
