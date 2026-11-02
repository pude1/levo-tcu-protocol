#!/usr/bin/env python3
"""Find CONFIG_LEVO_IMU_FORWARD_* for your mounting from a raw IMU log.

1. Build with CONFIG_LEVO_RIDE_RAW_LOG=y, flash, and run `idf.py monitor | tee orient.log`.
2. Stand the bike level for 3 s, then lift the FRONT wheel ~30 cm and hold for 3 s.
3. python3 tools/imu_orientation.py orient.log

Gravity defines up; the sensor axis that gains the most +g while the nose rises is
forward. The final mounting tilt is removed later by the on-bike level calibration."""
import json
import sys


def samples(path):
    for line in open(path, errors='replace'):
        i = line.find('{')
        if i < 0:
            continue
        try:
            m = json.loads(line[i:])
        except ValueError:
            continue
        if m.get('type') == 'imu':
            yield m['us'], [v / 1000 for v in m['a']]


def main():
    data = list(samples(sys.argv[1]))
    if len(data) < 400:
        sys.exit('need at least 2 s of imu lines')
    t0 = data[0][0]
    level = [a for t, a in data if t - t0 < 2_000_000]
    g = [sum(a[i] for a in level) / len(level) for i in range(3)]
    up = max(range(3), key=lambda i: abs(g[i]))
    names = 'XYZ'
    print('gravity (m/s^2):', ['%.2f' % v for v in g], '-> up axis %s%s' % ('+' if g[up] > 0 else '-', names[up]))
    best, best_axis = 0.0, None
    for t, a in data:
        for i in range(3):
            if i == up:
                continue
            for sign in (1, -1):
                d = sign * (a[i] - g[i])
                if d > best:
                    best, best_axis = d, (i, sign)
    if not best_axis or best < 2.0:
        sys.exit('no clear nose-up tilt found; lift the front wheel higher')
    i, sign = best_axis
    tag = ('POS_' if sign > 0 else 'NEG_') + names[i]
    print('forward axis: %s%s (peak +%.1f m/s^2)' % ('+' if sign > 0 else '-', names[i], best))
    print('sdkconfig: CONFIG_LEVO_IMU_FORWARD_%s=y' % tag)
    if up == 2:
        print('sdkconfig: CONFIG_LEVO_IMU_UP_AXIS=%d' % (0 if g[2] > 0 else 1))
    else:
        print('note: the board is mounted on its side; the level calibration handles this')


if __name__ == '__main__':
    main()
