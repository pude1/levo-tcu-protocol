"""End-to-end: synthetic USB log -> tools/ride_replay.py -> C ride pipeline."""
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
G = 9.80665


def synthetic_hop_log():
    lines, t, dt = [], 1_000_000, 5000
    pitch = 0.0
    speed = (int(14.4 * 100)).to_bytes(2, 'little') + b'\0\0\0\0'

    def emit(target, air=False, impact=0.0):
        nonlocal t, pitch
        rate = (target - pitch) / (dt * 1e-6)
        pitch = target
        t += dt
        if air:
            a = [0, 0, 0]
        else:  # mount: forward = -Y, up = +Z, left = +X
            a = [0, -G * math.sin(pitch), G * math.cos(pitch) + impact * G]
        g = [-rate, 0, 0]
        lines.append(json.dumps({'type': 'imu', 'us': t, 'a': [int(v * 1000) for v in a],
                                 'g': [int(v * 1000) for v in g]}))
        if (t // dt) % 10 == 0:
            lines.append('I (123) x: ' + json.dumps({'type': 'can_rx', 'us': t, 'id': 0x201, 'dlc': 6,
                                                     'data': speed.hex()}))

    for _ in range(800):
        emit(0)
    for i in range(1, 51):
        emit(math.radians(12) * i / 50)
    for i in range(1, 21):
        emit(math.radians(12 - 10 * i / 20))
    for _ in range(84):
        emit(0, air=True)
    for _ in range(10):
        emit(0, impact=1.5)
    for _ in range(400):
        emit(0)
    return '\n'.join(lines) + '\n'


class RideReplay(unittest.TestCase):
    def test_hop_detected(self):
        with tempfile.NamedTemporaryFile('w', suffix='.log', delete=False) as f:
            f.write(synthetic_hop_log())
        out = subprocess.run([sys.executable, str(ROOT / 'tools' / 'ride_replay.py'), f.name,
                              '--forward', '3'], capture_output=True, text=True, check=True)
        events = [json.loads(l) for l in out.stdout.splitlines() if l.startswith('{')]
        self.assertEqual([e['trick'] for e in events], ['BUNNY HOP'])
        self.assertTrue(18 < events[0]['height_cm'] < 26)


if __name__ == '__main__':
    unittest.main()
