#pragma once

#include "esp_err.h"

typedef struct {
    const char *phase;      // "PRELAUNCH", "ASCENT", ...
    float altitude;         // metres
    float apoapsis;         // metres
    float periapsis;        // metres
    float speed;            // m/s
    float throttle;         // 0..1
    float time_to_apoapsis; // seconds
} display_data_t;

// Starts the I2C bus, initialises the OLED and starts the refresh task.
esp_err_t display_init(void);

// Hand the latest values to the display. Fast, safe to call every packet.
void display_update(const display_data_t *data);