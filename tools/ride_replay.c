/* Host replay of a recorded ride through the firmware's ride pipeline.
 * Input lines (stdin):  I <us> ax ay az gx gy gz   (m/s^2, rad/s, sensor frame)
 *                       S <us> <km/h>               (CAN 0x201 speed)
 *                       C <us> <rpm>                (CAN 0x203 cadence)
 * Output: one JSON line per detected trick, plus a 10 Hz trace with --trace.
 * Options: --forward N (0:+X 1:-X 2:+Y 3:-Y), --up N (0:+Z 1:-Z), --level  (use the
 *          first 2 s as level calibration). Build: see tools/ride_replay.py. */
#include "ride.h"
#include "terrain_assist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    unsigned forward = 0, up = 0;
    int trace = 0, level = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--trace"))
            trace = 1;
        else if (!strcmp(argv[i], "--level"))
            level = 1;
        else if (!strcmp(argv[i], "--forward") && i + 1 < argc)
            forward = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--up") && i + 1 < argc)
            up = (unsigned)atoi(argv[++i]);
    }
    att_mount_t m;
    att_mount_default(&m, forward, up);
    ride_t r;
    ride_init(&r, &m);
    terrain_cfg_t tc;
    terrain_cfg_default(&tc);
    terrain_assist_t ta;
    char line[256];
    uint64_t first = 0, next_trace = 0;
    double gsum[3] = {0};
    unsigned gn = 0, tricks = 0;
    bool calibrated = !level, auto_on = false;
    while (fgets(line, sizeof(line), stdin)) {
        char kind;
        unsigned long long us;
        float v[6];
        if (sscanf(line, " %c %llu", &kind, &us) != 2)
            continue;
        if (!first)
            first = us;
        if (kind == 'S' && sscanf(line, " %*c %*u %f", &v[0]) == 1)
            ride_speed(&r, us, v[0]);
        else if (kind == 'C' && sscanf(line, " %*c %*u %f", &v[0]) == 1)
            ride_cadence(&r, us, v[0]);
        else if (kind == 'I' &&
                 sscanf(line, " %*c %*u %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4],
                        &v[5]) == 6) {
            att_raw_t raw = {v[0], v[1], v[2], v[3], v[4], v[5]};
            if (!calibrated) {
                gsum[0] += v[0], gsum[1] += v[1], gsum[2] += v[2];
                if (++gn >= 50 && us - first >= 2000000) {
                    float g[3] = {(float)(gsum[0] / gn), (float)(gsum[1] / gn),
                                  (float)(gsum[2] / gn)};
                    if (att_mount_from_gravity(&m, g, forward))
                        ride_init(&r, &m);
                    calibrated = true;
                }
                continue;
            }
            trick_event_t e;
            challenge_result_t cr;
            if (ride_imu_sample(&r, us, &raw, &e, &cr)) {
                tricks++;
                printf("{\"t\":%.2f,\"trick\":\"%s\",\"ms\":%u,\"dist_m\":%.1f,\"angle\":%.1f,"
                       "\"height_cm\":%.1f,\"rot\":%.0f,\"land_g\":%.2f,\"medal\":\"%s\",\"pb\":%s}\n",
                       (us - first) * 1e-6, trick_name(e.type), e.duration_ms, e.distance_m,
                       e.max_angle_deg, e.height_cm, e.rotation_deg, e.landing_g,
                       medal_name(cr.medal), cr.personal_best ? "true" : "false");
            }
            ride_output_t o = ride_output(&r, us);
            if (!auto_on && o.grade_valid) {
                terrain_assist_enable(&ta, us, 2);
                auto_on = true;
            }
            unsigned lvl = auto_on ? terrain_assist_update(&ta, &tc, us, o.grade_pct,
                                                           o.grade_valid, o.hold)
                                   : 0;
            if (trace && us >= next_trace) {
                printf("TRACE t=%.1f pitch=%.1f roll=%.1f rel=%.1f grade=%.1f%% hold=%d state=%d "
                       "auto=%u g=%.2f\n",
                       (us - first) * 1e-6, o.pitch_deg, o.roll_deg, o.pitch_rel_deg,
                       o.grade_pct, o.hold, o.trick_state, lvl, r.att.specific_force);
                next_trace = us + 100000;
            }
        }
    }
    fprintf(stderr, "%u tricks, %u points\n", tricks, (unsigned)r.book.points);
    return 0;
}
