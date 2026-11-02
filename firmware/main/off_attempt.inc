/* Standalone workstand control. Stock TCU absent. Starts OFF, no USB dependency. */
static unsigned selected_mode;
static bool selected_walk;
static uint32_t last_tx_alerts;
typedef struct {
    twai_handle_t bus;
    twai_status_info_t status;
    bool have_status;
} cleanup_context_t;
static bool cleanup_isolate(void *context) {
    (void)context;
    return idle_pins();
}
static bool cleanup_status(void *context, can_cleanup_state_t *state) {
    cleanup_context_t *c = context;
    c->have_status = twai_get_status_info_v2(c->bus, &c->status) == ESP_OK;
    if (!c->have_status)
        return false;
    *state = c->status.state == TWAI_STATE_RUNNING   ? CAN_CLEANUP_RUNNING
             : c->status.state == TWAI_STATE_STOPPED ? CAN_CLEANUP_STOPPED
             : c->status.state == TWAI_STATE_BUS_OFF ? CAN_CLEANUP_BUS_OFF
                                                     : CAN_CLEANUP_UNKNOWN;
    return true;
}
static bool cleanup_stop(void *context) {
    return twai_stop_v2(((cleanup_context_t *)context)->bus) == ESP_OK;
}
static bool cleanup_uninstall(void *context) {
    return twai_driver_uninstall_v2(((cleanup_context_t *)context)->bus) == ESP_OK;
}
static bool bench_send(twai_handle_t bus, const tcu_diag_frame_t *f) {
    /* Allow only the previously validated OFF/Eco/Trail/Turbo profiles. */
    if (f->id == 0x300) {
        if (f->len != 8 || f->data[1] != (selected_walk ? 0xa5 : 0x5a) || f->data[5] != 0)
            return false;
        bool off = !selected_walk && f->data[0] == 0 && f->data[4] == 0 && f->data[6] == 10;
        static const uint8_t support[] = {0, 35, 50, 100}, peak[] = {10, 35, 70, 100};
        bool enabled = (!selected_walk || selected_mode == 1) && selected_mode >= 1 &&
                       selected_mode <= 3 && f->data[0] == 3 &&
                       f->data[4] == support[selected_mode] && f->data[6] == peak[selected_mode] &&
                       f->data[2] == 0x64 && f->data[3] == 0x5a;
        if (!off && !enabled)
            return false;
    } else if (f->id == 0x301) {
        if (f->len != 3)
            return false;
    } else if (f->id == 0x100) {
        if (!((f->len == 8 && f->data[0] == 3 && f->data[1] == 0x22) ||
              (f->len == 3 && f->data[0] == 0x30)))
            return false;
    } else
        return false;
    twai_message_t tx = {.identifier = f->id, .data_length_code = f->len, .ss = 0};
    memcpy(tx.data, f->data, f->len);
    if (twai_transmit_v2(bus, &tx, pdMS_TO_TICKS(10)) != ESP_OK)
        return false;
    last_tx_alerts = 0;
    bool ok =
        twai_read_alerts_v2(bus, &last_tx_alerts, pdMS_TO_TICKS(20)) == ESP_OK &&
        (last_tx_alerts & TWAI_ALERT_TX_SUCCESS) &&
        !(last_tx_alerts & (TWAI_ALERT_TX_FAILED | TWAI_ALERT_BUS_ERROR | TWAI_ALERT_BUS_OFF));
    return ok;
}
static void off_attempt(bool wait_for_wake) {
    selected_mode = 0;
    selected_walk = false;
    ui_set(UI_WAIT, 0);
    twai_handle_t bus = NULL;
    bool installed = false, ok = false, diag_started = false;
    const char *stage = "install";
    unsigned sent = 0, pairs = 0, received = 0;
    int64_t last_received = 0;
    tcu_seq_t seq = {0};
    tcu_diag_t diag = {0};
    tcu_diag_frame_t f;
    state = "OFF START TEST";
    display_set_mode("ASSIST OFF ONLY", state);
    twai_general_config_t g =
        TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_17, GPIO_NUM_7, TWAI_MODE_NORMAL);
    g.controller_id = 0;
    g.tx_queue_len = 1;
    g.rx_queue_len = 64;
    g.intr_flags = ESP_INTR_FLAG_IRAM;
    g.alerts_enabled =
        TWAI_ALERT_TX_SUCCESS | TWAI_ALERT_TX_FAILED | TWAI_ALERT_BUS_ERROR | TWAI_ALERT_BUS_OFF;
    twai_timing_config_t timing = TWAI_TIMING_CONFIG_250KBITS();
    twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install_v2(&g, &timing, &filter, &bus) != ESP_OK) {
        control_fault_set(CONTROL_CAN_INIT, 0);
        goto cleanup;
    }
    installed = true;
    stage = "start";
    if (twai_start_v2(bus) != ESP_OK) {
        control_fault_set(CONTROL_CAN_START, 0);
        goto cleanup;
    }
    pin_audit("CAN_normal_started");
    last_tx_alerts = 0;
    if (wait_for_wake) {
        state = "TAP WAKE RESISTOR";
        display_set_mode("ASSIST OFF ONLY", state);
        puts("{\"type\":\"waiting_for_wake\",\"wait_seconds\":0,\"data_tx_started\":false,\"can_"
             "ack_enabled\":true}");
        int64_t motor_at = 0, next_wait_status = 0;
        twai_message_t rx;
        stage = "wake_timeout";
        while (true) {
            poll_usb();
            int64_t waiting_now = esp_timer_get_time();
            if (waiting_now >= next_wait_status) {
                int remaining = 0;
                char label[32];
                snprintf(label, sizeof(label), "WAITING FOR BIKE");
                display_set_mode("TCU ARMED", label);
                display_update(received, 0, 0, gpio_get_level(GPIO_NUM_7), last_received);
                printf("{\"type\":\"wake_wait\",\"seconds_left\":%d,\"rx_frames\":%u}\n", remaining,
                       received);
                next_wait_status = waiting_now + 1000000;
            }
            esp_err_t receive_result = twai_receive_v2(bus, &rx, pdMS_TO_TICKS(10));
            if (receive_result != ESP_OK) {
                if (receive_result != ESP_ERR_TIMEOUT) {
                    control_fault_set(CONTROL_CAN_RX, 0);
                    goto cleanup;
                }
                uint32_t alerts = 0;
                if (twai_read_alerts_v2(bus, &alerts, 0) == ESP_OK &&
                    (alerts & TWAI_ALERT_BUS_OFF)) {
                    control_fault_set(CONTROL_CAN_BUS, 0);
                    goto cleanup;
                }
                continue;
            }
            received++;
            last_received = esp_timer_get_time();
            printf("{\"type\":\"wake_can_rx\",\"us\":%" PRId64
                   ",\"id\":%u,\"ext\":%u,\"rtr\":%u,\"dlc\":%u,\"data\":\"",
                   esp_timer_get_time(), (unsigned)rx.identifier, rx.extd, rx.rtr,
                   rx.data_length_code);
            if (!rx.rtr)
                for (unsigned k = 0; k < rx.data_length_code && k < 8; k++)
                    printf("%02x", rx.data[k]);
            puts("\"}");
            if (!rx.extd && (rx.identifier == 0x300 || rx.identifier == 0x301)) {
                stage = "stock_tcu_detected";
                control_fault_set(CONTROL_TCU_CONFLICT, 0);
                goto cleanup;
            }
            if (!rx.extd && !rx.rtr && rx.identifier == 0x201 && rx.data_length_code == 6) {
                motor_at = esp_timer_get_time();
                break;
            }
        }
        if (!motor_at)
            goto cleanup;
        printf("{\"type\":\"motor_wake_seen\",\"us\":%" PRId64 "}\n", motor_at);
        while (esp_timer_get_time() - motor_at < 174000)
            vTaskDelay(pdMS_TO_TICKS(1));
    }
    state = "OFF START TEST";
    display_set_mode("ASSIST OFF ONLY", state);
    puts("{\"type\":\"off_attempt_started\",\"assist\":\"off\",\"persistent\":true,\"requires_"
         "lease\":false}");
    uint64_t boot = esp_timer_get_time(), last_motor = boot;
    tcu_bench_control_t control;
    tcu_bench_init(&control, boot);
    uint64_t status_at = boot;
    bool ready = false, shutting = false;
    walk_test_t walk = {0};
    remote_buttons_t remote;
    remote_buttons_reset(&remote, boot);
    /* Terrain AUTO only picks among the validated Eco/Trail/Turbo profiles above. */
    terrain_assist_t terrain = {0};
    terrain_cfg_t terrain_cfg;
    terrain_cfg_default(&terrain_cfg);
    bool shown_auto = false;
    int shown_bias = 0;

    const uint8_t off[7] = {0, 0x5a, 0x64, 0x5a, 0, 0, 10};
    if (!tcu_seq_start(&seq, off, boot)) {
        control_fault_set(CONTROL_PROFILE, 0);
        goto cleanup;
    }
    while (true) {
        uint64_t now = esp_timer_get_time();
        twai_message_t rx;
        esp_err_t receive_result;
        while ((receive_result = twai_receive_v2(bus, &rx, 0)) == ESP_OK) {
            received++;
            last_received = esp_timer_get_time();
            printf("{\"type\":\"can_rx\",\"us\":%" PRIu64 ",\"id\":%u,\"dlc\":%u,\"data\":\"", now,
                   (unsigned)rx.identifier, rx.data_length_code);
            if (!rx.rtr)
                for (unsigned k = 0; k < rx.data_length_code && k < 8; k++)
                    printf("%02x", rx.data[k]);
            puts("\"}");
            if (!rx.extd && (rx.identifier == 0x300 || rx.identifier == 0x301)) {
                stage = "stock_tcu_detected";
                control_fault_set(CONTROL_TCU_CONFLICT, 0);
                goto cleanup;
            }
            if (!rx.extd && !rx.rtr && rx.identifier == 0x201 && rx.data_length_code == 6)
                last_motor = now;
            if (!rx.extd && !rx.rtr)
                ride_task_can(rx.identifier, rx.data, rx.data_length_code, now);
            if (!rx.extd && !rx.rtr)
                tcu_diag_receive(&diag, now / 1000, rx.identifier, rx.data, rx.data_length_code);
        }
        if (receive_result != ESP_ERR_TIMEOUT) {
            control_fault_set(CONTROL_CAN_RX, 0);
            goto cleanup;
        }
        if (!diag_started && now - boot >= 2148000) {
            tcu_diag_start(&diag, now / 1000);
            diag_started = true;
        }
        if (!ready && !control.stopping && seq.phase == TCU_SEQ_RUN &&
            diag.phase == TCU_DIAG_DONE) {
            ready = true;
            state = "OFF - READY";
            display_set_mode("ASSIST OFF", state);
            puts("{\"type\":\"tcu_ready\",\"usb_required\":false}");
        }
        bool was_stopping = control.stopping;
        bool clock_error = now < control.last_us || last_motor > now;
        tcu_bench_tick(&control, now, last_motor);
        if (!was_stopping && control.stopping)
            control_fault_set(clock_error ? CONTROL_CLOCK : CONTROL_MOTOR_TIMEOUT, 0);
        poll_usb();
        button_snapshot_t wb = button_snapshot();
        bool was_walking = selected_walk;
        uint64_t sample_now = esp_timer_get_time();
        bool fresh =
            wb.at > 0 && sample_now >= (uint64_t)wb.at && sample_now - (uint64_t)wb.at < 50000;
        bool permit_walk = ready && !control.stopping && (control.mode == 0 || was_walking);
        if (permit_walk && !walk.armed && !was_walking && wb.raw == 0 && wb.stable == 0)
            walk_hold_arm(&walk, sample_now);
        walk_test_tick(&walk, sample_now, permit_walk, fresh, wb.raw, wb.stable);
        if (walk.active && !was_walking) {
            if (!tcu_bench_select(&control, now, last_motor, ready, 1))
                walk.active = walk.armed = false;
        } else if (!walk.active && was_walking) {
            tcu_bench_select(&control, now, last_motor, ready, 0);
        }
        bool walking_now = walk.active;
        if (walking_now != was_walking)
            printf("{\"type\":\"walk_state\",\"active\":%s}\n", walking_now ? "true" : "false");
        /* Consume events while disabled too; no queued presses survive readiness
         * or Walk transitions. The remote must be fully released to re-arm. */
        bool input_enabled = ready && !control.stopping && !walking_now && !was_walking;
        unsigned requested = control.mode;
        ride_status_t ride = ride_task_status();
        bool ride_usable = LEVO_RIDE_ENABLED && ride.imu_ok;
        remote.event = 0;
        bool remote_changed = remote_buttons_poll(&remote, sample_now, wb.stable,
                                                  input_enabled && fresh, control.mode, &requested);
        unsigned event = remote.event;
        if (event)
            walk.armed = false;
        if (terrain.enabled && event) {
            /* In AUTO: +/- shift the terrain map one level, Specialized leaves AUTO. */
            remote_changed = false;
            if (event == 1) {
                terrain_assist_bias(&terrain, now, +1);
            } else if (event == 2) {
                if (terrain.bias < 0 && terrain.level == 1) {
                    terrain_assist_disable(&terrain);
                    requested = 0;
                    remote_changed = true;
                } else {
                    terrain_assist_bias(&terrain, now, -1);
                }
            } else if (event == 8) {
                terrain_assist_disable(&terrain);
                requested = 1;
                remote_changed = true;
            }
        } else if (event == 8 && control.mode == 3 && ride_usable && input_enabled) {
            /* Specialized cycle: Off -> Eco -> Trail -> Turbo -> AUTO -> Eco ... */
            terrain_assist_enable(&terrain, now, control.mode);
            remote_changed = false;
            puts("{\"type\":\"auto_assist\",\"enabled\":true}");
        }
        if (remote_changed)
            tcu_bench_select(&control, now, last_motor, ready, requested);
        if (remote.shutdown)
            tcu_bench_stop(&control, now);
        ui_action_t action;
        while (ui_next(&action)) {
            if (!input_enabled)
                continue;
            walk.armed = false;
            if (action == UI_SHUTDOWN)
                tcu_bench_stop(&control, now);
            else if (action == UI_CALIBRATE)
                ride_task_request_calibration();
            else if (terrain.enabled && (action == UI_PLUS || action == UI_MINUS))
                terrain_assist_bias(&terrain, now, action == UI_PLUS ? +1 : -1);
            else
                tcu_bench_select(&control, now, last_motor, ready,
                                 ui_requested(action, control.mode));
        }
        /* Terrain AUTO. Leaves AUTO (keeping the current level) if the IMU disappears. */
        if (terrain.enabled) {
            if (control.stopping || !ready || walking_now) {
                terrain_assist_disable(&terrain);
            } else if (!ride_usable) {
                terrain_assist_disable(&terrain);
                puts("{\"type\":\"auto_assist\",\"enabled\":false,\"reason\":\"imu\"}");
            } else {
                unsigned level = terrain_assist_update(&terrain, &terrain_cfg, now,
                                                       ride.out.grade_pct, ride.out.grade_valid,
                                                       ride.out.hold);
                if (level != control.mode)
                    tcu_bench_select(&control, now, last_motor, ready, level);
            }
        }
#if CONFIG_LEVO_LOOPOUT_GUARD_DEG > 0
        if (ride_usable && ride.out.trick_state == TS_LIFT && control.mode > 1 &&
            ride.out.pitch_rel_deg > CONFIG_LEVO_LOOPOUT_GUARD_DEG) {
            tcu_bench_select(&control, now, last_motor, ready, 1);
            if (terrain.enabled)
                terrain.level = terrain.pending = terrain.base = 1;
            puts("{\"type\":\"loopout_guard\"}");
        }
#endif
        if (terrain.enabled != shown_auto || terrain.bias != shown_bias) {
            shown_auto = terrain.enabled;
            shown_bias = terrain.bias;
            display_set_auto(shown_auto, shown_bias);
        }
        if (control.mode != selected_mode || walking_now != selected_walk) {
            uint8_t payload[7];
            tcu_bench_payload(&control, payload);
            if (walking_now) {
                if (!tcu_seq_set_walk_profile(&seq)) {
                    control_fault_set(CONTROL_PROFILE, 0);
                    goto cleanup;
                }
            } else if (!tcu_seq_set_profile(&seq, payload)) {
                control_fault_set(CONTROL_PROFILE, 0);
                goto cleanup;
            }
            selected_walk = walking_now;
            selected_mode = control.mode;
            const char *labels[] = {"ASSIST OFF", "ECO 35 / 35", "TRAIL 50 / 70", "TURBO 100/100"};
            display_set_mode("TCU REMOTE", labels[selected_mode]);
            printf("{\"type\":\"mode_changed\",\"mode\":%u,\"us\":%" PRIu64 "}\n", selected_mode,
                   now);
        }
        if (!shutting && tcu_bench_shutdown_due(&control, now)) {
            if (!tcu_seq_shutdown(&seq)) {
                control_fault_set(CONTROL_PROFILE, 0);
                goto cleanup;
            }
            shutting = true;
            puts("{\"type\":\"shutdown_started\"}");
        }
        if (now - status_at >= 1000000) {
            printf("{\"type\":\"tcu_status\",\"mode\":%u,\"stopping\":%s,\"sent\":%u,\"motor_age_"
                   "ms\":%" PRIu64 "}\n",
                   control.mode, control.stopping ? "true" : "false", sent,
                   (now - last_motor) / 1000);
            display_update(received, 0, 0, gpio_get_level(GPIO_NUM_7), last_received);
            status_at = now;
        }
        ui_set(control.stopping ? UI_STOPPING
               : walking_now    ? UI_WALK
               : ready          ? UI_LIVE
                                : UI_WAIT,
               control.mode);
        stage = "command_send";
        if (tcu_seq_poll(&seq, now, &f)) {
            if (!bench_send(bus, &f)) {
                control_fault_set(last_tx_alerts & (TWAI_ALERT_BUS_ERROR | TWAI_ALERT_BUS_OFF)
                                      ? CONTROL_CAN_BUS
                                      : CONTROL_CAN_TX,
                                  0);
                goto cleanup;
            }
            sent++;
            if (f.id == 0x301)
                pairs++;
        }
        now = esp_timer_get_time();
        stage = "diagnostic_send";
        if (tcu_diag_poll(&diag, now / 1000, &f)) {
            if (!bench_send(bus, &f)) {
                control_fault_set(last_tx_alerts & (TWAI_ALERT_BUS_ERROR | TWAI_ALERT_BUS_OFF)
                                      ? CONTROL_CAN_BUS
                                      : CONTROL_CAN_TX,
                                  0);
                goto cleanup;
            }
            sent++;
        }
        stage = "sequence_or_diagnostic_fault";
        if (seq.phase == TCU_SEQ_FAULT || diag.phase == TCU_DIAG_FAILED) {
            control_fault_set(seq.phase == TCU_SEQ_FAULT ? CONTROL_TIMING : CONTROL_DIAGNOSTIC,
                              seq.phase == TCU_SEQ_FAULT ? 0 : diag.index + 1);
            goto cleanup;
        }
        if (seq.phase == TCU_SEQ_DONE) {
            ok = diag.phase == TCU_DIAG_DONE;
            if (!ok)
                control_fault_set(CONTROL_DIAGNOSTIC, diag.index + 1);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (ok) {
        twai_status_info_t st;
        stage = "status";
        bool have_status = twai_get_status_info_v2(bus, &st) == ESP_OK;
        ok = have_status && !st.bus_error_count && !st.tx_failed_count && !st.rx_missed_count &&
             !st.rx_overrun_count && !st.msgs_to_tx;
        if (!ok)
            control_fault_set(!have_status                                ? CONTROL_CAN_STATUS
                              : st.rx_missed_count || st.rx_overrun_count ? CONTROL_CAN_RX
                              : st.bus_error_count                        ? CONTROL_CAN_BUS
                                                                          : CONTROL_CAN_TX,
                              0);
    }
cleanup:;
    cleanup_context_t cleanup_context = {.bus = bus};
    const can_cleanup_ops_t cleanup_ops = {.isolate = cleanup_isolate,
                                           .status = cleanup_status,
                                           .stop = cleanup_stop,
                                           .uninstall = cleanup_uninstall};
    can_cleanup_result_t cleanup_result = can_cleanup(installed, &cleanup_ops, &cleanup_context);
    if (cleanup_context.have_status && cleanup_context.status.state == TWAI_STATE_BUS_OFF) {
        ok = false;
        control_fault_set(CONTROL_CAN_BUS, 0);
    }
    if (!cleanup_result.isolated || !cleanup_result.driver_released) {
        ok = false;
        control_fault_set(CONTROL_CAN_STATUS, 0);
    }
    control_fault_t fault = control_fault_get();
    if (fault.code != CONTROL_OK)
        printf("{\"type\":\"control_fault\",\"code\":%u,\"name\":\"%s\",\"detail\":%u}\n",
               (unsigned)fault.code, control_fault_name(fault.code), (unsigned)fault.detail);
    if (cleanup_context.have_status) {
        twai_status_info_t st = cleanup_context.status;
        printf("{\"type\":\"off_bus_status\",\"state\":%d,\"tx_errors\":%u,\"rx_errors\":%u,\"bus_"
               "errors\":%u,\"tx_failed\":%u,\"arbitration_lost\":%u,\"last_alerts\":%u}\n",
               st.state, (unsigned)st.tx_error_counter, (unsigned)st.rx_error_counter,
               (unsigned)st.bus_error_count, (unsigned)st.tx_failed_count,
               (unsigned)st.arb_lost_count, (unsigned)last_tx_alerts);
    }
    printf("{\"type\":\"can_cleanup\",\"tx_isolated\":%s,\"driver_released\":%s,"
           "\"stop_attempted\":%s,\"stop_succeeded\":%s}\n",
           cleanup_result.isolated ? "true" : "false",
           cleanup_result.driver_released ? "true" : "false",
           cleanup_result.stop_attempted ? "true" : "false",
           cleanup_result.stop_succeeded ? "true" : "false");
    printf("{\"type\":\"stack_margin\",\"minimum_free_bytes\":%u}\n",
           (unsigned)uxTaskGetStackHighWaterMark(NULL));
    selected_mode = 0;
    selected_walk = false;
    ui_set(UI_PREVIEW, 0);
    state = ok ? "OFF SEQ DONE" : "OFF TEST STOP";
    if (!ok)
        failures++;
    verified = sent;
    display_set_mode("ASSIST OFF ONLY", state);
    printf("{\"type\":\"off_attempt_result\",\"ok\":%s,\"stage\":\"%s\",\"sent\":%u,\"pairs\":%u,"
           "\"diagnostics\":%u,\"controller_stopped\":%s}\n",
           ok ? "true" : "false", stage, sent, pairs, diag.index,
           cleanup_result.driver_released ? "true" : "false");
    for (unsigned i = 0; i < diag.index; i++) {
        printf("{\"type\":\"diagnostic_value\",\"did\":%u,\"data\":\"", diag.results[i].did);
        for (unsigned k = 0; k < diag.results[i].len; k++)
            printf("%02x", diag.results[i].data[k]);
        puts("\"}");
    }
}
