#!/usr/bin/env python3
"""Replay a USB log (built with CONFIG_LEVO_RIDE_RAW_LOG=y) through the ride pipeline.

Usage:  python3 tools/ride_replay.py ride.log [--trace] [--forward N] [--up N] [--level]

The log is the JSON-lines USB output of the firmware (e.g. `idf.py monitor | tee ride.log`).
It needs `imu` lines (raw IMU) and `can_rx` lines for IDs 0x201/0x203. Change the
thresholds in ride/*.c, rerun, and compare: no bike required."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def convert(log_path):
    out = []
    for raw in open(log_path, errors='replace'):
        i = raw.find('{')
        if i < 0:
            continue
        try:
            m = json.loads(raw[i:])
        except ValueError:
            continue
        t = m.get('type')
        if t == 'imu':
            a, g = m['a'], m['g']
            out.append('I %d %.3f %.3f %.3f %.4f %.4f %.4f' % (
                m['us'], a[0] / 1000, a[1] / 1000, a[2] / 1000, g[0] / 1000, g[1] / 1000, g[2] / 1000))
        elif t == 'can_rx' and m.get('id') in (0x201, 0x203):
            d = bytes.fromhex(m.get('data', ''))
            if m['id'] == 0x201 and len(d) == 6:
                out.append('S %d %.2f' % (m['us'], int.from_bytes(d[0:2], 'little') / 100))
            elif m['id'] == 0x203 and len(d) == 8:
                out.append('C %d %.2f' % (m['us'], int.from_bytes(d[0:2], 'little') / 40))
    out.sort(key=lambda s: int(s.split()[1]))
    return '\n'.join(out) + '\n'


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('log')
    p.add_argument('--trace', action='store_true')
    p.add_argument('--level', action='store_true', help='use the first 2 s as level calibration')
    p.add_argument('--forward', type=int, default=3)
    p.add_argument('--up', type=int, default=0)
    a = p.parse_args()
    exe = Path(tempfile.gettempdir()) / 'levo_ride_replay'
    srcs = [str(s) for s in sorted((ROOT / 'ride').glob('*.c'))]
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-I', str(ROOT / 'ride'),
                    str(ROOT / 'tools' / 'ride_replay.c'), *srcs, '-lm', '-o', str(exe)], check=True)
    args = [str(exe), '--forward', str(a.forward), '--up', str(a.up)]
    args += ['--trace'] if a.trace else []
    args += ['--level'] if a.level else []
    sys.exit(subprocess.run(args, input=convert(a.log).encode()).returncode)


if __name__ == '__main__':
    main()
