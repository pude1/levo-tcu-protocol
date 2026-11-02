#include <stdio.h>
#include "async_log.h"
#define printf async_log_printf
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_jd9853.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "display.h"
#include "touch_controls.h"
#include "font.h"
#include "control_fault.h"
#include "ride_task.h"
#include <math.h>
#include <stdlib.h>

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    uint32_t count, missed, errors;
    int rx;
    int64_t last;
} stats;
static char mode_label[16], status_label[16];
static uint32_t display_bitrate = 250000;
static bool auto_on;
static int auto_bias;
static uint16_t *pixels;
static SemaphoreHandle_t done;
static esp_lcd_panel_handle_t panel;
static void flush(void) {
    for (int y = 0; y < 320; y += 20) {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, y, 172, y + 20, pixels + y * 172));
        if (xSemaphoreTake(done, pdMS_TO_TICKS(2000)) != pdTRUE) {
            printf("{\"type\":\"display_error\",\"reason\":\"spi_completion_timeout\"}\n");
            abort();
        }
    }
}
static bool finished(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx) {
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(done, &wake);
    return wake == pdTRUE;
}
static void text(int x, int y, const char *s, uint16_t color) {
    uint16_t wire_color = __builtin_bswap16(color);
    for (; *s; ++s, x += 10) {
        if (*s < 32 || *s > 126)
            continue;
        for (int row = 0; row < 20 && y + row < 320; ++row)
            for (int col = 0; col < 10 && x + col < 172; ++col)
                if (glyphs[(unsigned char)*s - 32][row] & (1 << col))
                    pixels[(y + row) * 172 + x + col] = wire_color;
    }
}
static void box(int x, int y, int w, int h, const char *label, bool selected, bool enabled) {
    uint16_t color = __builtin_bswap16(selected ? 0x03c0 : enabled ? 0x2104 : 0x1082);
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++)
            pixels[yy * 172 + xx] = color;
    text(x + (w - (int)strlen(label) * 10) / 2, y + (h - 20) / 2, label, enabled ? 0xffff : 0x8410);
}
static void large_text(int x, int y, const char *s, int scale, uint16_t color) {
    uint16_t c = __builtin_bswap16(color);
    for (; *s; s++, x += 10 * scale) {
        if (*s < 32 || *s > 126)
            continue;
        for (int row = 0; row < 20; row++)
            for (int col = 0; col < 10; col++)
                if (glyphs[(unsigned char)*s - 32][row] & (1 << col))
                    for (int dy = 0; dy < scale; dy++)
                        for (int dx = 0; dx < scale; dx++) {
                            int xx = x + col * scale + dx, yy = y + row * scale + dy;
                            if (xx >= 0 && xx < 172 && yy >= 0 && yy < 320)
                                pixels[yy * 172 + xx] = c;
                        }
    }
}
/* Trick result banner over the + area for four seconds. */
static void trick_toast(const ride_status_t *r) {
    static const uint16_t medal_color[] = {0x4208, 0xcb00, 0xa514, 0xfe60};
    uint16_t bg = __builtin_bswap16(medal_color[r->result.medal & 3]);
    for (int y = 88; y < 194; y++)
        for (int x = 8; x < 164; x++)
            pixels[y * 172 + x] = bg;
    const char *name = trick_name(r->event.type);
    int scale = strlen(name) * 20 <= 150 ? 2 : 1;
    large_text((172 - (int)strlen(name) * 10 * scale) / 2, 94, name, scale, 0xffff);
    char line[24];
    const trick_event_t *e = &r->event;
    if (e->type == TRICK_HOP)
        snprintf(line, sizeof(line), "%dcm", (int)(e->height_cm + 0.5f));
    else
        snprintf(line, sizeof(line), "%u.%us", (unsigned)(e->duration_ms / 1000),
                 (unsigned)(e->duration_ms % 1000 / 100));
    large_text((172 - (int)strlen(line) * 20) / 2, 124, line, 2, 0xffff);
    if (e->type == TRICK_WHEELIE || e->type == TRICK_MANUAL)
        snprintf(line, sizeof(line), "%dm %ddeg", (int)(e->distance_m + 0.5f),
                 (int)(e->max_angle_deg + 0.5f));
    else if (r->result.spin360 || r->result.spin180)
        snprintf(line, sizeof(line), "%s %s", r->result.spin360 ? "360!" : "180!",
                 medal_name(r->result.medal));
    else
        snprintf(line, sizeof(line), "%s%s", medal_name(r->result.medal),
                 r->result.personal_best ? " PB" : "");
    text((172 - (int)strlen(line) * 10) / 2, 168, line, 0xffff);
}
static void display_task(void *arg) {
    (void)arg;
    const char *modes[] = {"OFF", "ECO", "TRAIL", "TURBO"};
    const uint16_t colors[] = {0x0000, 0x0400, 0x001f, 0xf800};
    while (1) {
        ui_state_t state;
        unsigned mode;
        bool touch_ok;
        ui_get(&state, &mode, &touch_ok);
        if (mode > 3)
            mode = 0;
        bool enabled = state == UI_PREVIEW || state == UI_LIVE;
        memset(pixels, 0, 172 * 320 * 2);
        control_fault_t fault = control_fault_get();
        if (fault.code != CONTROL_OK) {
            text(6, 60, "ASSIST DISABLED", 0xf800);
            const char *label = control_fault_label(fault.code);
            text((172 - (int)strlen(label) * 10) / 2, 110, label, 0xffff);
            char detail[24];
            snprintf(detail, sizeof(detail), "DETAIL %u", fault.detail);
            text(6, 150, detail, 0xffff);
            text(6, 210, "POWER CYCLE", 0x8410);
            text(6, 240, "TO RESTART", 0x8410);
            flush();
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        for (int y = 0; y < 78; y++)
            for (int x = 0; x < 172; x++)
                pixels[y * 172 + x] = __builtin_bswap16(colors[mode]);
        large_text((172 - (int)strlen(state == UI_WALK ? "WALK" : modes[mode]) * 20) / 2, 3,
                   state == UI_WALK ? "WALK" : modes[mode], 2, 0xffff);
        bool show_auto;
        int bias;
        portENTER_CRITICAL(&lock);
        show_auto = auto_on;
        bias = auto_bias;
        portEXIT_CRITICAL(&lock);
        ride_status_t ride = ride_task_status();
        int64_t now_us = esp_timer_get_time();
        char auto_label[20], cal_label[16] = "";
        int g10 = (int)(ride.out.grade_pct * 10.0f);
        snprintf(auto_label, sizeof(auto_label), "AUTO%s %c%d.%d%%",
                 bias > 0 ? "+" : bias < 0 ? "-" : "", g10 < 0 ? '-' : '+', abs(g10) / 10,
                 abs(g10) % 10);
        if (ride.cal != RIDE_CAL_NONE && now_us - (int64_t)ride.cal_at < 3000000)
            snprintf(cal_label, sizeof(cal_label), "%s",
                     ride.cal == RIDE_CAL_RUNNING ? "HOLD STILL"
                     : ride.cal == RIDE_CAL_OK    ? "LEVEL SAVED"
                                                  : "LEVEL FAILED");
        const char *status = cal_label[0]             ? cal_label
                             : !touch_ok              ? "TOUCH OFFLINE"
                             : show_auto && state == UI_LIVE ? auto_label
                             : state == UI_WALK       ? "RELEASE TO STOP"
                             : state == UI_WALK_ARMED ? "WALK ARMED"
                             : state == UI_PREVIEW    ? "PREVIEW"
                             : state == UI_LIVE       ? "BIKE ON"
                             : state == UI_STOPPING   ? "SHUTTING DOWN"
                                                      : "WAITING";
        text((172 - (int)strlen(status) * 10) / 2, 53, status, 0xffff);
        box(8, 88, 156, 106, "", false, enabled);
        box(8, 204, 156, 108, "", false, enabled);
        large_text(66, 101, "+", 4, enabled ? 0xffff : 0x8410);
        large_text(66, 208, "-", 4, enabled ? 0xffff : 0x8410);
        if (ride.imu_ok && state == UI_LIVE && !show_auto) {
            char grade[20];
            snprintf(grade, sizeof(grade), "GRADE %c%d.%d%%", g10 < 0 ? '-' : '+', abs(g10) / 10,
                     abs(g10) % 10);
            text((172 - (int)strlen(grade) * 10) / 2, 288, grade, 0xffff);
        } else {
            text(26, 288, "HOLD 2s: OFF", enabled ? 0xffff : 0x8410);
        }
        if (ride.has_event && now_us - (int64_t)ride.event_at < 4000000)
            trick_toast(&ride);
        flush();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
void display_set_mode(const char *mode, const char *status) {
    char m[16] = {0}, s[16] = {0};
    if (mode)
        snprintf(m, sizeof(m), "%s", mode);
    if (status)
        snprintf(s, sizeof(s), "%s", status);
    portENTER_CRITICAL(&lock);
    memcpy(mode_label, m, sizeof(m));
    memcpy(status_label, s, sizeof(s));
    portEXIT_CRITICAL(&lock);
}
void display_set_auto(bool enabled, int bias) {
    portENTER_CRITICAL(&lock);
    auto_on = enabled;
    auto_bias = bias;
    portEXIT_CRITICAL(&lock);
}
void display_set_bitrate(uint32_t bitrate) {
    portENTER_CRITICAL(&lock);
    display_bitrate = bitrate;
    portEXIT_CRITICAL(&lock);
}
void display_update(uint32_t count, uint32_t missed, uint32_t errors, int rx, int64_t last_frame) {
    portENTER_CRITICAL(&lock);
    stats.count = count;
    stats.missed = missed;
    stats.errors = errors;
    stats.rx = rx;
    stats.last = last_frame;
    portEXIT_CRITICAL(&lock);
}
void display_start(void) {
    printf("{\"type\":\"display_init\",\"stage\":\"starting\"}\n");
    // Board pin assignments and JD9853 initialization from Waveshare's demo.
    // No SD card: GPIO4 belongs to the remote input test.
    gpio_set_direction(GPIO_NUM_23, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_23, 0);
    spi_bus_config_t bus = {.sclk_io_num = 1,
                            .mosi_io_num = 2,
                            .miso_io_num = -1,
                            .quadwp_io_num = -1,
                            .quadhd_io_num = -1,
                            .max_transfer_sz = 172 * 20 * 2};
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    done = xSemaphoreCreateBinary();
    configASSERT(done);
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t cfg = JD9853_PANEL_IO_SPI_CONFIG(14, 15, finished, NULL);
    cfg.pclk_hz = 40000000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &cfg, &io));
    esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = 22, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 16};
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9853(io, &dev, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, 34, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    pixels = heap_caps_malloc(172 * 320 * 2, MALLOC_CAP_DMA);
    configASSERT(pixels);
    ledc_timer_config_t timer = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                 .duty_resolution = LEDC_TIMER_10_BIT,
                                 .timer_num = LEDC_TIMER_0,
                                 .freq_hz = 5000,
                                 .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t backlight = {.gpio_num = 23,
                                       .speed_mode = LEDC_LOW_SPEED_MODE,
                                       .channel = LEDC_CHANNEL_0,
                                       .timer_sel = LEDC_TIMER_0,
                                       .duty = 800,
                                       .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&backlight));
    memset(pixels, 0, 172 * 320 * 2);
    large_text(6, 142, "LEVO TCU", 2, 0xffff);
    flush();
    printf("{\"type\":\"display_init\",\"stage\":\"first_frame_sent\"}\n");
    vTaskDelay(pdMS_TO_TICKS(1500));
    BaseType_t task_result = xTaskCreate(display_task, "can_display", 4096, NULL, 2, NULL);
    configASSERT(task_result == pdPASS);
}
