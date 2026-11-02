#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/twai.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_intr_alloc.h"
#include "display.h"
#include "touch_controls.h"
#include "walk_test.h"
#include "async_log.h"
#include "remote_buttons.h"
#include "control_fault.h"
#include "reset_policy.h"
#include "can_cleanup.h"
#include "tcu_sequence.h"
#include "tcu_bench_control.h"
#include "tcu_diagnostics.h"
#include "levo_protocol.h"
#include "terrain_assist.h"
#include "ride_task.h"
#include "sdkconfig.h"
#if CONFIG_LEVO_RIDE_IMU
#define LEVO_RIDE_ENABLED 1
#else
#define LEVO_RIDE_ENABLED 0
#endif
#define printf async_log_printf
#define puts async_log_puts
static const char *state = "IDLE";
static unsigned verified, failures;
static bool idle_pins(void) {
    esp_err_t level = gpio_set_level(GPIO_NUM_17, 1);
    /* IDF gpio_output_enable (via set_direction) restores the GPIO matrix's
     * simple output route. TWAI can no longer drive CTX, even if uninstall fails. */
    esp_err_t tx = gpio_set_direction(GPIO_NUM_17, GPIO_MODE_OUTPUT);
    esp_err_t rx = gpio_set_direction(GPIO_NUM_7, GPIO_MODE_INPUT);
    return level == ESP_OK && tx == ESP_OK && rx == ESP_OK;
}
static void pin_audit(const char *stage) {
    (void)stage;
}
typedef struct {
    unsigned raw, stable, samples, minus_low;
    int64_t at, max_gap;
} button_snapshot_t;
static button_snapshot_t buttons;
static portMUX_TYPE buttons_lock = portMUX_INITIALIZER_UNLOCKED;
static button_snapshot_t button_snapshot(void) {
    button_snapshot_t out;
    portENTER_CRITICAL(&buttons_lock);
    out = buttons;
    portEXIT_CRITICAL(&buttons_lock);
    return out;
}
static void button_sample_task(void *unused) {
    (void)unused;
    const int pins[] = {3, 4, 16, 8};
    int stable[4] = {1, 1, 1, 1}, candidate[4] = {1, 1, 1, 1};
    int64_t since[4] = {0}, last = 0, max_gap = 0;
    unsigned samples = 0, minus_low = 0;
    for (;;) {
        int64_t now = esp_timer_get_time();
        unsigned raw = 0, mask = 0;
        if (last && now - last > max_gap) {
            max_gap = now - last;
        }
        last = now;
        for (int i = 0; i < 4; i++) {
            int level = gpio_get_level(pins[i]);
            if (!level) {
                raw |= 1U << i;
                if (i == 1)
                    minus_low++;
            }
            if (level != candidate[i]) {
                candidate[i] = level;
                since[i] = now;
            }
            if (level != stable[i] && now - since[i] >= 30000)
                stable[i] = level;
            if (!stable[i])
                mask |= 1U << i;
        }
        button_snapshot_t b = {.raw = raw,
                               .stable = mask,
                               .samples = ++samples,
                               .minus_low = minus_low,
                               .at = now,
                               .max_gap = max_gap};
        portENTER_CRITICAL(&buttons_lock);
        buttons = b;
        portEXIT_CRITICAL(&buttons_lock);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
static void report_buttons(void) {
    static int64_t previous_at;
    static unsigned previous_mask = 99;
    button_snapshot_t b = button_snapshot();
    int64_t now = esp_timer_get_time();
    if (b.stable != previous_mask || now - previous_at >= 1000000) {
        printf("{\"type\":\"button_sampler\",\"raw\":%u,\"stable\":%u,\"samples\":%u,\"minus_low_"
               "samples\":%u,\"age_us\":%lld,\"max_gap_us\":%lld}\n",
               b.raw, b.stable, b.samples, b.minus_low, (long long)(now - b.at),
               (long long)b.max_gap);
        previous_at = now;
        previous_mask = b.stable;
    }
}
static void remote_init(void) {
    gpio_config_t cfg = {.pin_bit_mask = (1ULL << 3) | (1ULL << 4) | (1ULL << 16) | (1ULL << 8),
                         .mode = GPIO_MODE_INPUT,
                         .pull_up_en = GPIO_PULLUP_ENABLE,
                         .pull_down_en = GPIO_PULLDOWN_DISABLE,
                         .intr_type = GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&cfg));
}
static void poll_usb(void);
#include "off_attempt.inc"
static void poll_usb(void) {
    report_buttons();
}
#if CONFIG_LEVO_SNIFF_ONLY
/* Listen-only: the controller never drives the bus (no ACK, no error frames, no data).
 * Logs every frame for comparison with docs/can-protocol.md and feeds the ride
 * pipeline, so grade and trick detection can be tried while the stock TCU rides. */
static void sniff_only(void) {
    twai_handle_t bus = NULL;
    twai_general_config_t g =
        TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_17, GPIO_NUM_7, TWAI_MODE_LISTEN_ONLY);
    g.rx_queue_len = 64;
    g.tx_queue_len = 0;
    twai_timing_config_t timing = TWAI_TIMING_CONFIG_250KBITS();
    twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install_v2(&g, &timing, &filter, &bus) != ESP_OK || twai_start_v2(bus) != ESP_OK) {
        control_fault_set(CONTROL_CAN_INIT, 0);
        return;
    }
    display_set_mode("SNIFF ONLY", "LISTENING");
    ui_set(UI_PREVIEW, 0);
    puts("{\"type\":\"sniff_only\",\"transmit\":false}");
    unsigned received = 0;
    for (;;) {
        twai_message_t rx;
        poll_usb();
        if (twai_receive_v2(bus, &rx, pdMS_TO_TICKS(10)) != ESP_OK)
            continue;
        uint64_t now = esp_timer_get_time();
        received++;
        printf("{\"type\":\"can_rx\",\"us\":%" PRIu64 ",\"id\":%u,\"dlc\":%u,\"data\":\"", now,
               (unsigned)rx.identifier, rx.data_length_code);
        if (!rx.rtr)
            for (unsigned k = 0; k < rx.data_length_code && k < 8; k++)
                printf("%02x", rx.data[k]);
        puts("\"}");
        if (!rx.extd && !rx.rtr)
            ride_task_can(rx.identifier, rx.data, rx.data_length_code, now);
        if (!rx.extd && rx.identifier == 0x300 && rx.data_length_code == 8 &&
            !levo_command_valid(rx.data, 8))
            printf("{\"type\":\"sniff_check_mismatch\",\"us\":%" PRIu64 "}\n", now);
        display_update(received, 0, 0, gpio_get_level(GPIO_NUM_7), (int64_t)now);
    }
}
#endif
static void start_ui(void *unused) {
    (void)unused;
    touch_controls_start();
    display_start();
    vTaskDelete(NULL);
}
void app_main(void) {
    bool pins_ok = idle_pins();
    if (!pins_ok)
        control_fault_set(CONTROL_CAN_INIT, 0);
    esp_reset_reason_t reset = esp_reset_reason();
    bool reset_blocked = reset_requires_stop(reset);
    if (reset_blocked)
        control_fault_set(CONTROL_RESET, (uint16_t)reset);
    remote_init();
    async_log_start();
#if CONFIG_LEVO_RIDE_IMU
    ride_task_start(); /* reads NVS before any CAN traffic; IMU optional */
#endif
    configASSERT(xTaskCreate(button_sample_task, "buttons", 3072, NULL, 4, NULL) == pdPASS);
    ui_set(UI_WAIT, 0);
    configASSERT(xTaskCreate(start_ui, "ui_init", 6144, NULL, 2, NULL) == pdPASS);
    vTaskPrioritySet(NULL, 5);
    printf("{\"type\":\"standalone_boot\",\"build\":\"%s\",\"assist\":\"off\","
           "\"usb_required\":false,\"reset_reason\":%u,\"reset_blocked\":%s}\n",
           esp_app_get_description()->version, (unsigned)reset, reset_blocked ? "true" : "false");
    if (reset_blocked || !pins_ok) {
        display_set_mode("ASSIST OFF", "RESET STOP");
    } else {
#if CONFIG_LEVO_SNIFF_ONLY
        (void)off_attempt;
        sniff_only();
#else
        off_attempt(true);
#endif
    }
#if CONFIG_LEVO_RIDE_IMU
    ride_task_persist(); /* CAN is stopped: flash writes can no longer disturb timing */
#endif
    // Stay stopped if powered by USB. Buck power should disappear on shutdown.
    // Never automatically retry and re-wake after a fault or deliberate stop.
    ui_set(UI_STOPPING, 0);
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}
