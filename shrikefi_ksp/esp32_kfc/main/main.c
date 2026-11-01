
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "cJSON.h"

#define WIFI_SSID       "Second floor 2"
#define WIFI_PASSWORD   "12341234"

#define TELEMETRY_PORT  5006
#define COMMAND_PORT    5007

#define PC_IP           "192.168.0.101"

#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "SHRIKE_KFC";

static EventGroupHandle_t wifi_events;

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Wi-Fi disconnected. Reconnecting...");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        ESP_LOGI(TAG, "Got IP: " IPSTR,
                 IP2STR(&event->ip_info.ip));

        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,
        wifi_event_handler, NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};

    strcpy((char *)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char *)wifi_config.sta.password, WIFI_PASSWORD);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(
        WIFI_IF_STA, &wifi_config));

    ESP_ERROR_CHECK(esp_wifi_start());

    xEventGroupWaitBits(
        wifi_events,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY);
}

static void telemetry_task(void *arg)
{
    char rx_buffer[512];

    struct sockaddr_in local_address = {0};
    local_address.sin_family = AF_INET;
    local_address.sin_port = htons(TELEMETRY_PORT);
    local_address.sin_addr.s_addr = htonl(INADDR_ANY);

    int rx_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

    if (rx_socket < 0) {
        ESP_LOGE(TAG, "Could not create receive socket");
        vTaskDelete(NULL);
        return;
    }

    if (bind(rx_socket,
             (struct sockaddr *)&local_address,
             sizeof(local_address)) < 0) {
        ESP_LOGE(TAG, "Could not bind UDP port");
        close(rx_socket);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in pc_address = {0};
    pc_address.sin_family = AF_INET;
    pc_address.sin_port = htons(COMMAND_PORT);
    pc_address.sin_addr.s_addr = inet_addr(PC_IP);

    int tx_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

    if (tx_socket < 0) {
        ESP_LOGE(TAG, "Could not create transmit socket");
        close(rx_socket);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Listening for telemetry on UDP %d",
             TELEMETRY_PORT);

    while (1) {
        struct sockaddr_in sender_address;
        socklen_t sender_length = sizeof(sender_address);

        int length = recvfrom(
            rx_socket,
            rx_buffer,
            sizeof(rx_buffer) - 1,
            0,
            (struct sockaddr *)&sender_address,
            &sender_length);

        if (length <= 0) {
            continue;
        }

        rx_buffer[length] = '\0';

        cJSON *root = cJSON_Parse(rx_buffer);

        if (root == NULL) {
            ESP_LOGW(TAG, "Received invalid JSON");
            continue;
        }

        cJSON *seq = cJSON_GetObjectItem(root, "seq");
        cJSON *altitude = cJSON_GetObjectItem(root, "altitude");
        cJSON *vertical_speed =
            cJSON_GetObjectItem(root, "vertical_speed");
        cJSON *apoapsis = cJSON_GetObjectItem(root, "apoapsis");

        if (cJSON_IsNumber(seq) &&
            cJSON_IsNumber(altitude) &&
            cJSON_IsNumber(vertical_speed) &&
            cJSON_IsNumber(apoapsis)) {

            ESP_LOGI(TAG,
                "Seq:%d Alt:%.1f m VSpeed:%.1f m/s AP:%.1f m",
                seq->valueint,
                altitude->valuedouble,
                vertical_speed->valuedouble,
                apoapsis->valuedouble);
        }

        cJSON_Delete(root);

        // Safe response: never apply control in this first test.
        const char *safe_command =
            "{\"enable\":0,\"throttle\":0,"
            "\"pitch\":0,\"yaw\":0,\"roll\":0}";

        sendto(
            tx_socket,
            safe_command,
            strlen(safe_command),
            0,
            (struct sockaddr *)&pc_address,
            sizeof(pc_address));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ShrikeFi KSP flight computer starting");

    wifi_init();

    ESP_LOGI(TAG, "Wi-Fi connected. Starting telemetry task.");

    xTaskCreate(
        telemetry_task,
        "telemetry_task",
        4096,
        NULL,
        5,
        NULL);
}
