// AXS5106L register layout and portrait orientation follow Waveshare's
// ESP32-C6-Touch-LCD-1.47 demo (register 0x01, I2C address 0x63).
#include "touch_controls.h"
#include "board_i2c.h"
#include <stdio.h>
#include "async_log.h"
#define printf async_log_printf
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static ui_state_t screen_state = UI_PREVIEW;
static unsigned screen_mode, epoch;
static bool online;
static QueueHandle_t events;
static i2c_master_dev_handle_t device;
typedef struct {
    ui_action_t action;
    unsigned epoch;
    int64_t at;
} event_t;
void ui_set(ui_state_t s, unsigned m) {
    portENTER_CRITICAL(&lock);
    if (screen_state != s) {
        screen_state = s;
        epoch++;
    }
    screen_mode = m;
    portEXIT_CRITICAL(&lock);
}
void ui_get(ui_state_t *s, unsigned *m, bool *ok) {
    portENTER_CRITICAL(&lock);
    *s = screen_state;
    *m = screen_mode;
    *ok = online;
    portEXIT_CRITICAL(&lock);
}
bool ui_next(ui_action_t *a) {
    event_t e;
    while (events && xQueueReceive(events, &e, 0) == pdTRUE) {
        portENTER_CRITICAL(&lock);
        unsigned current = epoch;
        portEXIT_CRITICAL(&lock);
        if (e.epoch == current && esp_timer_get_time() - e.at <= 500000) {
            *a = e.action;
            return true;
        }
    }
    return false;
}
static void touch_task(void *unused) {
    (void)unused;
    int candidate = UI_NONE, last_x = 0, last_y = 0;
    unsigned gesture_epoch = 0, up_samples = 0;
    bool released = false, pressed = false, cancelled = false;
    int64_t down_at = 0, last_error = 0;
    for (;;) {
        uint8_t reg = 1, b[14] = {0};
        esp_err_t err = i2c_master_transmit(device, &reg, 1, 20);
        if (err == ESP_OK)
            err = i2c_master_receive(device, b, sizeof(b), 20);
        int64_t now = esp_timer_get_time();
        if (err != ESP_OK) {
            released = false;
            pressed = false;
            up_samples = 0;
            candidate = UI_NONE;
            portENTER_CRITICAL(&lock);
            online = false;
            portEXIT_CRITICAL(&lock);
            if (now - last_error > 2000000) {
                printf("{\"type\":\"touch_error\",\"error\":%d}\n", err);
                last_error = now;
            }
        } else {
            portENTER_CRITICAL(&lock);
            online = true;
            unsigned current = epoch;
            ui_state_t state = screen_state;
            portEXIT_CRITICAL(&lock);
            unsigned points = b[1] & 15;
            if (points == 0) {
                if (pressed && !cancelled && candidate != UI_NONE && current == gesture_epoch &&
                    now - down_at >= (candidate == UI_SHUTDOWN ? 2000000 : 30000) &&
                    now - down_at <= 10000000 && (state == UI_PREVIEW || state == UI_LIVE)) {
                    event_t e = {candidate, current, now};
                    xQueueSend(events, &e, 0);
                    printf("{\"type\":\"touch_tap\",\"action\":%d,\"x\":%d,\"y\":%d}\n", candidate,
                           last_x, last_y);
                }
                pressed = false;
                candidate = UI_NONE;
                if (++up_samples >= 2)
                    released = true;
            } else {
                up_samples = 0;
                int raw_x = ((b[2] & 15) << 8) | b[3], y = ((b[4] & 15) << 8) | b[5];
                int x = 171 - raw_x;
                int hit = (points == 1 && x >= 0 && x < 172 && y < 320) ? ui_hit(x, y) : UI_NONE;
                if (!pressed) {
                    pressed = true;
                    cancelled = !released;
                    released = false;
                    candidate = hit;
                    gesture_epoch = current;
                    down_at = now;
                    printf("{\"type\":\"touch_down\",\"x\":%d,\"y\":%d,\"points\":%u}\n", x, y,
                           points);
                }
                if (hit != candidate || current != gesture_epoch || points != 1)
                    cancelled = true;
                ui_action_t hold = ui_hold_action(candidate, now - down_at);
                if (!cancelled && hold != UI_NONE && current == gesture_epoch && state == UI_LIVE) {
                    event_t e = {hold, current, now};
                    if (xQueueSend(events, &e, 0) == pdTRUE) {
                        cancelled = true; // One event per hold; suppress the following release tap.
                        printf("{\"type\":\"touch_hold\",\"action\":%d,\"held_ms\":%lld}\n", hold,
                               (long long)((now - down_at) / 1000));
                    }
                }
                last_x = x;
                last_y = y;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
void touch_controls_start(void) {
    events = xQueueCreate(8, sizeof(event_t));
    configASSERT(events);
    gpio_config_t rst = {.pin_bit_mask = 1ULL << 20, .mode = GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&rst));
    gpio_set_level(20, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(20, 1);
    vTaskDelay(pdMS_TO_TICKS(300));
    i2c_master_bus_handle_t bus = board_i2c_bus();
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x63, .scl_speed_hz = 400000};
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev, &device));
    configASSERT(xTaskCreate(touch_task, "touch", 3072, NULL, 2, NULL) == pdPASS);
}
