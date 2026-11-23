
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>

#include "display.h"

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

#define WIFI_CONNECTED_BIT BIT0

typedef enum { PRELAUNCH, ASCENT, COAST, CIRCULARIZE, DONE } phase_t;

typedef struct {
    float throttle;
    float target_pitch;
    float heading;
} command_t;

static phase_t phase = PRELAUNCH;

static const char *TAG = "SHRIKE_KFC";

static EventGroupHandle_t wifi_events;

typedef struct {
    uint32_t sequence;

    float altitude;
    float vertical_speed;
    float surface_speed;

    float apoapsis;
    float periapsis;

    float time_to_apoapsis;

    float pitch;
    float heading;
    float throttle;

    bool valid;
} rocket_state_t;

static rocket_state_t rocket = {0};

static const char *phase_name(phase_t p)
{
    switch (p) {
        case PRELAUNCH:   return "PRELAUNCH";
        case ASCENT:      return "ASCENT";
        case COAST:       return "COAST";
        case CIRCULARIZE: return "CIRCULARIZE";
        case DONE:        return "ORBIT";
    }
    return "?";
}

static command_t decide(const rocket_state_t *r)
{
    command_t cmd = { 0, 90, 90 };      // safe defaults: throttle 0, pitch 90, heading 90

    // If the bridge was restarted, its counter resets, so go back to PRELAUNCH
    static uint32_t last_seq = 0;
    if (r->sequence < last_seq) {
        phase = PRELAUNCH;
    }
    last_seq = r->sequence;

    switch (phase) {

        case PRELAUNCH:
            if (r->sequence > 200) {
                phase = ASCENT;
                ESP_LOGI("FLIGHT", "Launching, seq=%lu", (unsigned long)r->sequence);
            }
            break;

        case ASCENT: {
            // Pitch: 90 deg at 0 km, 0 deg at 45 km
            float pitch = 90.0f - 90.0f * (r->altitude / 45000.0f);
            if (pitch < 0.0f)  pitch = 0.0f;
            if (pitch > 90.0f) pitch = 90.0f;

            cmd.target_pitch = pitch;
            cmd.heading = 90;
            cmd.throttle = (r->apoapsis < 95000.0f) ? 1.0f : 0.2f;

            if (r->apoapsis >= 100000.0f) {
                phase = COAST;
                ESP_LOGI("FLIGHT", "Target apoapsis reached, coasting");
            }
            break;
        }

        case COAST:
            cmd.throttle = 0;
            cmd.target_pitch = 0;
            cmd.heading = 90;

            if (r->time_to_apoapsis < 15.0f) {
                phase = CIRCULARIZE;
                ESP_LOGI("FLIGHT", "Circularizing");
            }
            break;

        case CIRCULARIZE:
            cmd.throttle = 1.0f;
            cmd.target_pitch = 0;
            cmd.heading = 90;

            if (r->periapsis >= 80000.0f) {
                phase = DONE;
                ESP_LOGI("FLIGHT", "Orbit reached");
            }
            break;

        case DONE:
            // defaults: throttle 0
            break;
    }
    return cmd;
}

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
        wifi_event_sta_disconnected_t *event =
            (wifi_event_sta_disconnected_t *)event_data;
        xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason=%d). Reconnecting...",
                 event->reason);
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
        cJSON *alt = cJSON_GetObjectItem(root, "altitude");
        cJSON *vs = cJSON_GetObjectItem(root, "vertical_speed");
        cJSON *ss = cJSON_GetObjectItem(root, "surface_speed");
        cJSON *ap = cJSON_GetObjectItem(root, "apoapsis");
        cJSON *pe = cJSON_GetObjectItem(root, "periapsis");
        cJSON *pi = cJSON_GetObjectItem(root, "pitch");
        cJSON *hd = cJSON_GetObjectItem(root, "heading");
        cJSON *th = cJSON_GetObjectItem(root, "throttle");
        cJSON *tta = cJSON_GetObjectItem(root, "time_to_apoapsis");

        if (cJSON_IsNumber(seq) &&
            cJSON_IsNumber(alt) &&
            cJSON_IsNumber(vs) &&
            cJSON_IsNumber(ss) &&
            cJSON_IsNumber(ap) &&
            cJSON_IsNumber(pe) &&
            cJSON_IsNumber(pi) &&
            cJSON_IsNumber(hd) &&
            cJSON_IsNumber(tta) &&
            cJSON_IsNumber(th)) {

            rocket.sequence = (uint32_t)seq->valuedouble;
            rocket.altitude = (float)alt->valuedouble;
            rocket.vertical_speed = (float)vs->valuedouble;
            rocket.surface_speed = (float)ss->valuedouble;
            rocket.apoapsis = (float)ap->valuedouble;
            rocket.periapsis = (float)pe->valuedouble;
            rocket.pitch = (float)pi->valuedouble;
            rocket.heading = (float)hd->valuedouble;
            rocket.throttle = (float)th->valuedouble;
            rocket.time_to_apoapsis = (float)tta->valuedouble;

            rocket.valid = true;

            ESP_LOGI(TAG,
                "ALT: %.1f | V_SPEED: %.1f | APO: %.1f | PITCH: %.1f |TIME_TO_APO: %.1f",
                rocket.altitude,
                rocket.vertical_speed,
                rocket.apoapsis,
                rocket.pitch,
                rocket.time_to_apoapsis);
        }

        cJSON_Delete(root);

        if (!rocket.valid) continue;
        command_t cmd = decide(&rocket);

        display_update(&(display_data_t){
            .phase = phase_name(phase),
            .altitude = rocket.altitude,
            .apoapsis = rocket.apoapsis,
            .periapsis = rocket.periapsis,
            .speed = rocket.surface_speed,
            .throttle = cmd.throttle,
            .time_to_apoapsis = rocket.time_to_apoapsis,
        });

        char out[128];

        snprintf(out, sizeof(out),"{\"enable\":%d,\"throttle\":%.2f,\"target_pitch\":%.1f,\"heading\":%.1f}",phase == PRELAUNCH ? 0 : 1,cmd.throttle, cmd.target_pitch, cmd.heading);

        sender_address.sin_port = htons(COMMAND_PORT);   // reply to the sender, on the command port

        int sent = sendto(tx_socket, out, strlen(out), 0,
                        (struct sockaddr *)&sender_address, sizeof(sender_address));
        ESP_LOGI(TAG, "Command sendto returned %d", sent);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ShrikeFi KSP flight computer starting");

    wifi_init();

    ESP_LOGI(TAG, "Wi-Fi connected. Starting telemetry task.");

    if (display_init() != ESP_OK) {
        ESP_LOGW(TAG, "OLED not found, continuing without display");
    }

    xTaskCreate(
        telemetry_task,
        "telemetry_task",
        4096,
        NULL,
        5,
        NULL);
}
