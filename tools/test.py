#!/usr/bin/env python3
"""Run portable controller tests. No device access or CAN transmission."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--no-sanitizers', action='store_true', help='For compilers without ASan/UBSan')
a = p.parse_args()
os.chdir(ROOT)
build = ROOT / '.build-tests'
build.mkdir(exist_ok=True)
compiler = os.environ.get('CC', 'cc')
flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-g', '-Iprotocol', '-Iride', '-Ifirmware/main']
if not a.no_sanitizers:
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
# Only the standalone policy is linked. The old USB-lease bench policy is not shipped.
sources = [str(p) for p in sorted((ROOT/'protocol').glob('*.c'))]
sources += [str(p) for p in sorted((ROOT/'ride').glob('*.c'))]
sources += [
    'firmware/main/remote_buttons.c', 'firmware/main/walk_test.c',
    'firmware/main/standalone_control.c', 'firmware/main/touch_policy.c',
    'firmware/main/control_fault.c',
]
fixtures = {'sequence_check': 'sequence-startup-fixture.txt',
            'diagnostics_check': 'diagnostics-startup-fixture.txt'}
count = 0
for test in sorted((ROOT/'tests').glob('*_check.c')):
    exe = build / test.stem
    test_flags = []
    test_sources = sources
    if test.stem == 'reset_policy_check':
        # Only this pure policy test sees the enum stub; firmware uses ESP-IDF.
        test_flags = ['-DLEVO_RESET_POLICY_HOST_TEST', '-Itests/stubs/reset_policy']
        test_sources = []
    subprocess.run([compiler, *flags, *test_flags, str(test), *test_sources,
                    '-lm', '-o', str(exe)], check=True)
    data = (ROOT/'tests'/fixtures[test.stem]).read_bytes() if test.stem in fixtures else b''
    if test.stem == 'codec_check':
        data = b'03 5a 64 5a 23 00 23 ea\n03 5a 64 5a 64 00 64 30\n'
    result = subprocess.run([str(exe)], input=data, capture_output=True)
    print(test.stem + ': ' + result.stdout.decode().strip(), flush=True)
    if result.returncode:
        sys.stderr.write(result.stderr.decode());sys.exit(result.returncode)
    count += 1
subprocess.run([sys.executable, '-B', '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_*.py'], check=True)
print(f'PASS: {count} C programs and Python codec tests. No hardware tested.')
