#!/usr/bin/env python3
"""Compare a sniff-only USB log from another bike (e.g. a Kenevo) with the documented protocol.

Usage: python3 tools/compare_capture.py kenevo-stock.log

Reads `can_rx` JSON lines (sniff-only or normal firmware) and reports:
  * 0x300 frames that fail the documented check nibble / counter rule
  * 0x301 companions that do not match the table for their counter
  * command period statistics
  * the distinct 0x300 payloads (bytes 0..6) seen, mapped to known Levo profiles
  * telemetry IDs present and their DLCs
Nothing is transmitted; this only reads a file."""
from collections import Counter, defaultdict
import json
from pathlib import Path
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'protocol'))
import levo  # noqa: E402

KNOWN = {
    '005a645a00000a': 'Off', '035a645a230023': 'Eco 35/35', '035a645a320046': 'Trail 50/70',
    '035a645a640064': 'Turbo 100/100', '03a5645a230023': 'Walk',
    '005a005a00000a': 'startup / shutdown phase 1', '005a00a500000a': 'shutdown final (Off)',
}


def frames(path):
    for line in open(path, errors='replace'):
        i = line.find('{')
        if i < 0:
            continue
        try:
            m = json.loads(line[i:])
        except ValueError:
            continue
        if m.get('type') in ('can_rx', 'wake_can_rx'):
            yield m['us'], m['id'], bytes.fromhex(m.get('data', ''))


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    bad_cmd = bad_comp = 0
    last_counter, cmd_times = None, []
    payloads, ids = Counter(), defaultdict(set)
    for us, ident, data in frames(sys.argv[1]):
        ids[ident].add(len(data))
        if ident == 0x300 and len(data) == 8:
            counter = data[7] & 15
            if levo.encode_command(data[:7], counter) != data:
                bad_cmd += 1
            payloads[data[:7].hex()] += 1
            cmd_times.append(us)
            last_counter = counter
        elif ident == 0x301 and len(data) == 3:
            n = data[1] & 15
            if data != levo.encode_companion(n) or (last_counter is not None and n != last_counter):
                bad_comp += 1
    n_cmd = sum(payloads.values())
    print(f'0x300 commands: {n_cmd}, check/counter mismatches: {bad_cmd}')
    print(f'0x301 companion mismatches: {bad_comp}')
    if len(cmd_times) > 10:
        gaps = [(b - a) / 1000 for a, b in zip(cmd_times, cmd_times[1:]) if b > a]
        print(f'command period ms: median {statistics.median(gaps):.1f}, '
              f'p95 {sorted(gaps)[int(len(gaps) * .95)]:.1f}')
    print('payloads (bytes 0..6):')
    for p, c in payloads.most_common():
        print(f'  {p}  x{c:<6} {KNOWN.get(p, "UNKNOWN - not in the Levo profile table")}')
    print('IDs seen:', ', '.join(f'0x{i:03x}/dlc{sorted(d)}' for i, d in sorted(ids.items())))
    expected = {0x201: 6, 0x202: 8, 0x203: 8}
    for i, dlc in expected.items():
        if dlc not in ids.get(i, set()):
            print(f'WARNING: telemetry 0x{i:03x} with DLC {dlc} not seen; ride features need 0x201/0x203')
    ok = n_cmd and not bad_cmd and not bad_comp
    print('RESULT:', 'matches the documented encoding' if ok else 'DIFFERENCES FOUND - do not replace the TCU yet')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
