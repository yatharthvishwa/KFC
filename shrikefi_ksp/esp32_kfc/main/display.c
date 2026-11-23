#include "display.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

// ---------------- Settings ----------------
#define OLED_SDA_GPIO    16
#define OLED_SCL_GPIO    15
#define OLED_ADDR        0x3C   // some modules use 0x3D
#define OLED_COL_OFFSET  0      // set to 2 if you have an SH1106 (1.3") screen
#define REFRESH_MS       200

#define OLED_W      128
#define OLED_H      64
#define OLED_PAGES  (OLED_H / 8)

static const char *TAG = "OLED";

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t dev;

static uint8_t fb[OLED_W * OLED_PAGES];     // the framebuffer

static display_data_t shared = { .phase = "BOOT" };
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

// ---------------- 5x7 font (ASCII 0x20 ' ' to 0x5A 'Z') ----------------
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14}, {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x56,0x20,0x50}, {0x00,0x08,0x07,0x03,0x00}, {0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00}, {0x2A,0x1C,0x7F,0x1C,0x2A}, {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x80,0x70,0x30,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x00,0x60,0x60,0x00},
    {0x20,0x10,0x08,0x04,0x02}, {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x72,0x49,0x49,0x49,0x46}, {0x21,0x41,0x49,0x4D,0x33}, {0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x31}, {0x41,0x21,0x11,0x09,0x07},
    {0x36,0x49,0x49,0x49,0x36}, {0x46,0x49,0x49,0x29,0x1E}, {0x00,0x00,0x14,0x00,0x00},
    {0x00,0x40,0x34,0x00,0x00}, {0x00,0x08,0x14,0x22,0x41}, {0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x59,0x09,0x06}, {0x3E,0x41,0x5D,0x59,0x4E},
    {0x7C,0x12,0x11,0x12,0x7C}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x41,0x3E}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x41,0x51,0x73}, {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x1C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
    {0x26,0x49,0x49,0x49,0x32}, {0x03,0x01,0x7F,0x01,0x03}, {0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
    {0x03,0x04,0x78,0x04,0x03}, {0x61,0x59,0x49,0x4D,0x43},
};

// ---------------- Low-level OLED I2C ----------------
static esp_err_t oled_commands(const uint8_t *cmds, size_t n)
{
    uint8_t buf[40];
    buf[0] = 0x00;                       // control byte: "the rest are commands"
    memcpy(&buf[1], cmds, n);
    return i2c_master_transmit(dev, buf, n + 1, 100);
}

static esp_err_t oled_flush(void)
{
    uint8_t buf[OLED_W + 1];
    buf[0] = 0x40;                       // control byte: "the rest is pixel data"

    for (int page = 0; page < OLED_PAGES; page++) {
        uint8_t set_page[] = {
            (uint8_t)(0xB0 | page),                          // page address
            (uint8_t)(OLED_COL_OFFSET & 0x0F),               // column low nibble
            (uint8_t)(0x10 | (OLED_COL_OFFSET >> 4)),        // column high nibble
        };
        esp_err_t err = oled_commands(set_page, sizeof(set_page));
        if (err != ESP_OK) return err;

        memcpy(&buf[1], &fb[page * OLED_W], OLED_W);
        err = i2c_master_transmit(dev, buf, sizeof(buf), 100);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

// ---------------- Drawing helpers (draw into fb) ----------------
static void px(int x, int y)
{
    if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) return;
    fb[(y / 8) * OLED_W + x] |= (1 << (y % 8));
}

static void line(int x0, int y0, int x1, int y1)     // Bresenham
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        px(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void rect(int x, int y, int w, int h)         // outline
{
    line(x, y, x + w - 1, y);
    line(x, y + h - 1, x + w - 1, y + h - 1);
    line(x, y, x, y + h - 1);
    line(x + w - 1, y, x + w - 1, y + h - 1);
}

static void fill_rect(int x, int y, int w, int h)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            px(x + i, y + j);
}

static void draw_text(int x, int y, const char *s)
{
    for (; *s; s++, x += 6) {
        char c = *s;
        if (c >= 'a' && c <= 'z') c -= 32;           // font is uppercase only
        if (c < 0x20 || c > 0x5A) c = '?';
        const uint8_t *glyph = font5x7[c - 0x20];
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 8; row++)
                if (glyph[col] & (1 << row))
                    px(x + col, y + row);
    }
}

// ---------------- Screen layout ----------------
static void draw_rocket(int cx, float throttle, int frame)
{
    // nose cone
    line(cx - 6, 18, cx, 6);
    line(cx, 6, cx + 6, 18);
    // body and window
    rect(cx - 6, 18, 13, 29);
    rect(cx - 2, 24, 5, 5);
    // fins
    line(cx - 6, 36, cx - 12, 50);
    line(cx - 12, 50, cx - 6, 46);
    line(cx + 6, 36, cx + 12, 50);
    line(cx + 12, 50, cx + 6, 46);
    // engine nozzle
    fill_rect(cx - 3, 47, 7, 3);
    // flame, longer with more throttle, flickers each frame
    if (throttle > 0.02f) {
        int len = 2 + (int)(throttle * 10.0f) + (frame & 1);
        line(cx - 3, 50, cx, 50 + len);
        line(cx + 3, 50, cx, 50 + len);
    }
}

static void render(const display_data_t *d)
{
    static int frame = 0;
    frame++;

    memset(fb, 0, sizeof(fb));

    // altitude bar on the far left (full = 100 km)
    float frac = d->altitude / 100000.0f;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    int fill = (int)(frac * 62);
    rect(0, 0, 5, 64);
    fill_rect(1, 63 - fill, 3, fill);

    draw_rocket(26, d->throttle, frame);

    char text[24];
    draw_text(48, 2, d->phase ? d->phase : "?");

    snprintf(text, sizeof(text), "ALT %.2fKM", d->altitude / 1000.0f);
    draw_text(48, 14, text);
    snprintf(text, sizeof(text), "APO %.1fKM", d->apoapsis / 1000.0f);
    draw_text(48, 24, text);
    snprintf(text, sizeof(text), "PE %.1fKM", d->periapsis / 1000.0f);
    draw_text(48, 34, text);
    snprintf(text, sizeof(text), "SPD %.0fM/S", d->speed);
    draw_text(48, 44, text);
    snprintf(text, sizeof(text), "TTA %.0fS", d->time_to_apoapsis);
    draw_text(48, 54, text);
}

// ---------------- Refresh task ----------------
static void display_task(void *arg)
{
    display_data_t d;
    while (1) {
        portENTER_CRITICAL(&lock);
        d = shared;
        portEXIT_CRITICAL(&lock);

        render(&d);
        if (oled_flush() != ESP_OK) {
            ESP_LOGW(TAG, "I2C write failed");
        }
        vTaskDelay(pdMS_TO_TICKS(REFRESH_MS));
    }
}

// ---------------- Public API ----------------
esp_err_t display_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = OLED_SDA_GPIO,
        .scl_io_num = OLED_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);
    if (err != ESP_OK) return err;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_ADDR,
        .scl_speed_hz = 400000,
    };
    err = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    if (err != ESP_OK) return err;

    const uint8_t init[] = {
        0xAE,             // display off
        0xD5, 0x80,       // clock divide
        0xA8, 0x3F,       // multiplex: 64 rows
        0xD3, 0x00,       // display offset
        0x40,             // start line 0
        0x8D, 0x14,       // charge pump on
        0x20, 0x02,       // page addressing mode
        0xA1,             // flip horizontally (use 0xA0 to undo)
        0xC8,             // flip vertically   (use 0xC0 to undo)
        0xDA, 0x12,       // COM pins
        0x81, 0xCF,       // contrast
        0xD9, 0xF1,       // precharge
        0xDB, 0x40,       // VCOMH
        0xA4,             // follow RAM
        0xA6,             // normal (not inverted)
        0xAF,             // display on
    };
    err = oled_commands(init, sizeof(init));
    if (err != ESP_OK) return err;      // fails here if the OLED isn't found

    xTaskCreate(display_task, "oled", 4096, NULL, 3, NULL);
    return ESP_OK;
}

void display_update(const display_data_t *data)
{
    portENTER_CRITICAL(&lock);
    shared = *data;
    portEXIT_CRITICAL(&lock);
}