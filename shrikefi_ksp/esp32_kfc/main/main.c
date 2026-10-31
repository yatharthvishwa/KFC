
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"

#define BLINK_GPIO GPIO_NUM_21

static const char *TAG = "BLINK";

void app_main(void)
{
    // Configure the GPIO pin as an output
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BLINK_GPIO), // make a bit mask with BLINK_GPIO bit set to 1
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    // Apply the GPIO configuration
    ESP_ERROR_CHECK(gpio_config(&io_conf)); //'&' passing the memory address of the structure, the driver reads the settings from that address and configures the hardware

    // ESP_LOGI(TAG, "Blinky program started!");

    while (1)
    {
        // Turn LED ON
        ESP_ERROR_CHECK(gpio_set_level(BLINK_GPIO, 1));
        printf("LED ON\n");
        // ESP_LOGI(TAG, "LED ON");

        // Wait for one second
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Turn LED OFF
        ESP_ERROR_CHECK(gpio_set_level(BLINK_GPIO, 0));
        printf("LED OFF\n");
        // ESP_LOGI(TAG, "LED OFF");

        // Wait for one second
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
