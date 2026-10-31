
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "SHRIKEFI";

void app_main(void)
{
    ESP_LOGI(TAG, "KSP flight computer started!");

    while (1)
    {
        ESP_LOGI(TAG, "ESP32-S3 is running...");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
