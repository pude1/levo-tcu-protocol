#include "ride_task.h"
#include <string.h>
#include "async_log.h"
#include "board_i2c.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "levo_protocol.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "qmi8658.h"
#include "sdkconfig.h"
#define printf async_log_printf

#ifndef CONFIG_LEVO_IMU_FORWARD_AXIS
#define CONFIG_LEVO_IMU_FORWARD_AXIS 0
#endif
#ifndef CONFIG_LEVO_IMU_UP_AXIS
#define CONFIG_LEVO_IMU_UP_AXIS 0
#endif
#ifndef CONFIG_LEVO_RIDE_LOG_HZ
#define CONFIG_LEVO_RIDE_LOG_HZ 5
#endif

#define NVS_NS "ride"
#define MOUNT_VERSION 1

typedef struct {
    uint32_t version;
    att_mount_t mount;
} stored_mount_t;

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static ride_status_t status;
static struct {
    float speed_kmh, cadence_rpm;
    uint64_t speed_at, cadence_at;
} can;
static volatile bool cal_requested;
static bool nvs_ready, mount_dirty, bests_dirty;
static stored_mount_t stored;
static float stored_best[TRICK_COUNT], live_best[TRICK_COUNT];
static ride_t ride;

void ride_task_can(uint32_t id, const uint8_t *data, uint8_t len, uint64_t now) {
    levo_telemetry_t t = {0};
    if (!levo_telemetry_decode(id, data, len, &t))
        return;
    portENTER_CRITICAL(&lock);
    if (id == 0x201) {
        can.speed_kmh = t.speed_kmh;
        can.speed_at = now;
    } else if (id == 0x203) {
        can.cadence_rpm = t.cadence_rpm;
        can.cadence_at = now;
    }
    portEXIT_CRITICAL(&lock);
}

ride_status_t ride_task_status(void) {
    ride_status_t s;
    portENTER_CRITICAL(&lock);
    s = status;
    portEXIT_CRITICAL(&lock);
    return s;
}

void ride_task_request_calibration(void) {
    cal_requested = true;
}

static void load_nvs(void) {
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        /* Do not erase here: a full/foreign partition just disables persistence. */
        printf("{\"type\":\"ride_nvs\",\"ok\":false,\"err\":%d}\n", err);
        return;
    }
    nvs_ready = true;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK)
        return;
    size_t n = sizeof(stored);
    if (nvs_get_blob(h, "mount", &stored, &n) != ESP_OK || n != sizeof(stored) ||
        stored.version != MOUNT_VERSION)
        stored.version = 0;
    n = sizeof(stored_best);
    if (nvs_get_blob(h, "best", stored_best, &n) != ESP_OK || n != sizeof(stored_best))
        memset(stored_best, 0, sizeof(stored_best));
    nvs_close(h);
}

void ride_task_persist(void) {
    if (!nvs_ready || (!mount_dirty && !bests_dirty))
        return;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK)
        return;
    bool ok = true;
    if (mount_dirty)
        ok = nvs_set_blob(h, "mount", &stored, sizeof(stored)) == ESP_OK;
    if (bests_dirty) {
        float best[TRICK_COUNT];
        portENTER_CRITICAL(&lock);
        memcpy(best, live_best, sizeof(best));
        portEXIT_CRITICAL(&lock);
        ok = nvs_set_blob(h, "best", best, sizeof(best)) == ESP_OK && ok;
    }
    ok = nvs_commit(h) == ESP_OK && ok;
    nvs_close(h);
    printf("{\"type\":\"ride_persist\",\"ok\":%s}\n", ok ? "true" : "false");
    if (ok)
        mount_dirty = bests_dirty = false;
}

static void log_status(const ride_status_t *s, uint64_t now) {
    printf("{\"type\":\"ride\",\"us\":%llu,\"imu\":%d,\"pitch_cdeg\":%d,\"roll_cdeg\":%d,"
           "\"rel_cdeg\":%d,\"grade_dpct\":%d,\"valid\":%d,\"hold\":%d,\"trick_state\":%d,"
           "\"g_milli\":%d}\n",
           (unsigned long long)now, s->imu_ok, (int)(s->out.pitch_deg * 100),
           (int)(s->out.roll_deg * 100), (int)(s->out.pitch_rel_deg * 100),
           (int)(s->out.grade_pct * 10), s->out.grade_valid, s->out.hold, s->out.trick_state,
           (int)(ride.att.specific_force * 1000));
}

static void log_event(const trick_event_t *e, const challenge_result_t *r) {
    printf("{\"type\":\"trick\",\"name\":\"%s\",\"ms\":%u,\"dist_dm\":%d,\"angle_cdeg\":%d,"
           "\"height_mm\":%d,\"rot_deg\":%d,\"land_mg\":%d,\"medal\":\"%s\",\"pb\":%s,"
           "\"points\":%u}\n",
           trick_name(e->type), (unsigned)e->duration_ms, (int)(e->distance_m * 10),
           (int)(e->max_angle_deg * 100), (int)(e->height_cm * 10), (int)e->rotation_deg,
           (int)(e->landing_g * 1000), medal_name(r->medal), r->personal_best ? "true" : "false",
           (unsigned)r->points);
}

static void ride_task(void *unused) {
    (void)unused;
    qmi8658_t imu = {0};
    bool present = false;
    unsigned failures = 0;
    uint64_t next_probe = 0, next_log = 0, cal_deadline = 0, last_speed = 0, last_cadence = 0;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(5));
        uint64_t now = esp_timer_get_time();
        if (!present) {
            if (now >= next_probe) {
                present = qmi8658_init(&imu, board_i2c_bus());
                printf("{\"type\":\"imu_probe\",\"found\":%s,\"addr\":%u}\n",
                       present ? "true" : "false", imu.address);
                next_probe = now + 2000000;
                failures = 0;
            }
            if (!present) {
                portENTER_CRITICAL(&lock);
                status.imu_ok = false;
                portEXIT_CRITICAL(&lock);
                continue;
            }
        }
        float kmh, rpm;
        uint64_t sat, cat;
        portENTER_CRITICAL(&lock);
        kmh = can.speed_kmh;
        rpm = can.cadence_rpm;
        sat = can.speed_at;
        cat = can.cadence_at;
        portEXIT_CRITICAL(&lock);
        if (sat != last_speed) {
            ride_speed(&ride, sat, kmh);
            last_speed = sat;
        }
        if (cat != last_cadence) {
            ride_cadence(&ride, cat, rpm);
            last_cadence = cat;
        }

        att_raw_t raw;
        bool fresh = false;
        if (!qmi8658_read(&imu, &raw, &fresh)) {
            if (++failures >= 40) { /* 200 ms of bus errors: re-probe */
                present = false;
                next_probe = now + 1000000;
                printf("{\"type\":\"imu_lost\"}\n");
            }
            continue;
        }
        failures = 0;
        if (!fresh)
            continue;
#if CONFIG_LEVO_RIDE_RAW_LOG
        printf("{\"type\":\"imu\",\"us\":%llu,\"a\":[%d,%d,%d],\"g\":[%d,%d,%d]}\n",
               (unsigned long long)now, (int)(raw.ax * 1000), (int)(raw.ay * 1000),
               (int)(raw.az * 1000), (int)(raw.gx * 1000), (int)(raw.gy * 1000),
               (int)(raw.gz * 1000));
#endif
        trick_event_t ev;
        challenge_result_t res;
        bool got = ride_imu_sample(&ride, now, &raw, &ev, &res);

        ride_cal_t cal = RIDE_CAL_NONE;
        if (cal_requested) {
            cal_requested = false;
            cal_deadline = now + 5000000;
            cal = RIDE_CAL_RUNNING;
        }
        if (cal_deadline) {
            float g[3];
            if (now >= cal_deadline) {
                cal = RIDE_CAL_FAILED;
                cal_deadline = 0;
            } else if (att_is_still(&ride.att, now) && now - ride.att.still_from >= 1500000 &&
                       att_still_gravity(&ride.att, now, g)) {
                att_mount_t m;
                if (att_mount_from_gravity(&m, g, CONFIG_LEVO_IMU_FORWARD_AXIS)) {
                    float bias[3];
                    memcpy(bias, ride.att.bias, sizeof(bias));
                    att_init(&ride.att, &m);
                    memcpy(ride.att.bias, bias, sizeof(bias));
                    stored = (stored_mount_t){.version = MOUNT_VERSION, .mount = m};
                    mount_dirty = true;
                    cal = RIDE_CAL_OK;
                } else {
                    cal = RIDE_CAL_FAILED;
                }
                cal_deadline = 0;
            }
        }
        if (got) {
            log_event(&ev, &res);
            if (res.personal_best)
                bests_dirty = true;
        }
        ride_status_t s;
        portENTER_CRITICAL(&lock);
        status.imu_ok = true;
        status.calibrated = stored.version == MOUNT_VERSION;
        status.out = ride_output(&ride, now);
        status.points = ride.book.points;
        if (got) {
            status.has_event = true;
            status.event = ev;
            status.result = res;
            status.event_at = now;
            memcpy(live_best, ride.book.best, sizeof(live_best));
        }
        if (cal != RIDE_CAL_NONE) {
            status.cal = cal;
            status.cal_at = now;
        }
        s = status;
        portEXIT_CRITICAL(&lock);
        if (CONFIG_LEVO_RIDE_LOG_HZ > 0 && now >= next_log) {
            log_status(&s, now);
            next_log = now + 1000000 / CONFIG_LEVO_RIDE_LOG_HZ;
        }
        if (cal == RIDE_CAL_OK || cal == RIDE_CAL_FAILED)
            printf("{\"type\":\"ride_calibration\",\"ok\":%s}\n",
                   cal == RIDE_CAL_OK ? "true" : "false");
    }
}

void ride_task_start(void) {
    load_nvs();
    att_mount_t m;
    if (stored.version == MOUNT_VERSION)
        m = stored.mount;
    else
        att_mount_default(&m, CONFIG_LEVO_IMU_FORWARD_AXIS, CONFIG_LEVO_IMU_UP_AXIS);
    ride_init(&ride, &m);
    memcpy(ride.book.best, stored_best, sizeof(stored_best));
    memcpy(live_best, stored_best, sizeof(live_best));
    printf("{\"type\":\"ride_start\",\"stored_mount\":%s}\n",
           stored.version == MOUNT_VERSION ? "true" : "false");
    configASSERT(xTaskCreate(ride_task, "ride_imu", 4096, NULL, 3, NULL) == pdPASS);
}
