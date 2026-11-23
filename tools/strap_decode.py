#!/usr/bin/env python3
"""Turns a walk's strap.bin (lib/strap/strap_file.h) into CSV files.

    python3 tools/strap_decode.py <strap.bin> [<out dir>]

Writes, next to strap.bin unless an out dir is given:

    pulse.csv   uptime_ms, bpm, contact             one row per heart rate notification
    rr.csv      uptime_ms, rr_ms                    one row per beat-to-beat interval
    ecg.csv     uptime_ms, strap_ns, microvolts     one row per ECG sample
    acc.csv     uptime_ms, strap_ns, x_mg, y_mg, z_mg
    notes.csv   uptime_ms, kind, text               the board's notes, Polar control replies, losses

uptime_ms is the board's clock when the record arrived (samples.csv has the
same clock in whole seconds, next to UTC time). strap_ns is the strap's own
clock for each sample: the frame's timestamp belongs to its last sample, the
ones before it are spaced at the nominal rate. Then prints a short summary.
"""

from __future__ import annotations

import csv
import sys
from dataclasses import dataclass, field
from pathlib import Path

HEADER_MAGIC = b"STRAP\x00"
HEADER_SIZE = 8
RECORD_HEADER_SIZE = 7

HEART_RATE, PMD_DATA, PMD_CONTROL, NOTE, LOST = 0x01, 0x02, 0x03, 0x10, 0x11
ECG, ACC = 0x00, 0x02
# What the board asks the strap for (lib/strap/pmd.h).
ECG_RATE_HZ = 130
ACC_RATE_HZ = 50

PMD_ERRORS = {
    0: "success", 1: "invalid op code", 2: "invalid measurement type", 3: "not supported",
    4: "invalid length", 5: "invalid parameter", 6: "already running", 7: "invalid resolution",
    8: "invalid sample rate", 9: "invalid range", 10: "MTU too small",
    11: "invalid number of channels", 12: "invalid state", 13: "strap is charging",
}
MEASUREMENT_NAMES = {0: "ECG", 1: "optical pulse", 2: "accelerometer", 3: "pulse intervals",
                     5: "gyroscope", 6: "magnetometer"}


@dataclass
class Decoded:
    pulse: list[tuple[int, int, str]] = field(default_factory=list)
    rr: list[tuple[int, float]] = field(default_factory=list)
    ecg: list[tuple[int, int, int]] = field(default_factory=list)
    acc: list[tuple[int, int, int, int, int]] = field(default_factory=list)
    notes: list[tuple[int, str, str]] = field(default_factory=list)
    frame_times: dict[int, list[tuple[int, int]]] = field(default_factory=dict)  # type -> (strap_ns, samples)
    unknown_frames: dict[str, int] = field(default_factory=dict)
    lost: int = 0
    cut_short: bool = False


def signed(chunk: bytes) -> int:
    return int.from_bytes(chunk, "little", signed=True)


def decode_heart_rate(uptime: int, p: bytes, out: Decoded) -> None:
    if len(p) < 2:
        return
    flags, pos = p[0], 1
    if flags & 0x01:
        bpm, pos = int.from_bytes(p[1:3], "little"), 3
    else:
        bpm, pos = p[1], 2
    contact = ("yes" if flags & 0x02 else "no") if flags & 0x04 else ""
    if flags & 0x08:
        pos += 2  # energy expended
    out.pulse.append((uptime, bpm, contact))
    if flags & 0x10:
        for i in range(pos, len(p) - 1, 2):
            out.rr.append((uptime, int.from_bytes(p[i:i + 2], "little") * 1000 / 1024))


def decode_pmd_data(uptime: int, p: bytes, out: Decoded) -> None:
    if len(p) < 10:
        return
    kind, last_ns, frame_type, samples = p[0], int.from_bytes(p[1:9], "little"), p[9], p[10:]
    if kind == ECG and frame_type == 0x00:
        values = [signed(samples[i:i + 3]) for i in range(0, len(samples) - 2, 3)]
        step = 1e9 / ECG_RATE_HZ
        n = len(values)
        out.ecg.extend((uptime, round(last_ns - (n - 1 - i) * step), v) for i, v in enumerate(values))
    elif kind == ACC and frame_type in (0x00, 0x01, 0x02):
        width = frame_type + 1  # bytes per axis
        size = 3 * width
        triples = [samples[i:i + size] for i in range(0, len(samples) - size + 1, size)]
        step = 1e9 / ACC_RATE_HZ
        n = len(triples)
        for i, t in enumerate(triples):
            x, y, z = (signed(t[a * width:(a + 1) * width]) for a in range(3))
            out.acc.append((uptime, round(last_ns - (n - 1 - i) * step), x, y, z))
        values = triples
    else:
        key = f"measurement {kind}, frame type 0x{frame_type:02x}"
        out.unknown_frames[key] = out.unknown_frames.get(key, 0) + 1
        return
    out.frame_times.setdefault(kind, []).append((last_ns, len(values)))


def describe_control(p: bytes) -> str:
    if len(p) >= 4 and p[0] == 0xF0:
        what = MEASUREMENT_NAMES.get(p[2], f"type {p[2]}")
        op = {1: "settings", 2: "start", 3: "stop"}.get(p[1], f"op {p[1]}")
        return f"{op} {what}: {PMD_ERRORS.get(p[3], f'error {p[3]}')}"
    if len(p) >= 2 and p[0] == 0x0F:
        offered = [name for bit, name in MEASUREMENT_NAMES.items() if p[1] & (1 << bit)]
        return "offers " + ", ".join(offered)
    return "bytes " + p.hex(" ")


def decode(data: bytes) -> Decoded:
    if len(data) < HEADER_SIZE or not data.startswith(HEADER_MAGIC):
        raise ValueError("not a strap.bin file")
    if data[6] != 1:
        raise ValueError(f"strap.bin format {data[6]}, this tool reads format 1")
    out = Decoded()
    pos = HEADER_SIZE
    while pos < len(data):
        if pos + RECORD_HEADER_SIZE > len(data):
            out.cut_short = True
            break
        kind = data[pos]
        uptime = int.from_bytes(data[pos + 1:pos + 5], "little")
        length = int.from_bytes(data[pos + 5:pos + 7], "little")
        payload = data[pos + RECORD_HEADER_SIZE:pos + RECORD_HEADER_SIZE + length]
        if len(payload) < length:
            out.cut_short = True
            break
        pos += RECORD_HEADER_SIZE + length
        if kind == HEART_RATE:
            decode_heart_rate(uptime, payload, out)
        elif kind == PMD_DATA:
            decode_pmd_data(uptime, payload, out)
        elif kind == PMD_CONTROL:
            out.notes.append((uptime, "polar", describe_control(payload)))
        elif kind == NOTE:
            out.notes.append((uptime, "board", payload.decode("ascii", "replace")))
        elif kind == LOST:
            count = int.from_bytes(payload[:4], "little")
            out.lost += count
            out.notes.append((uptime, "lost", f"{count} records"))
        else:
            out.notes.append((uptime, "unknown", f"record type {kind}, {length} bytes"))
    return out


def write_csv(path: Path, header: list[str], rows: list[tuple]) -> None:
    with path.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(rows)


def measured_rate(frames: list[tuple[int, int]]) -> str:
    if len(frames) < 2 or frames[-1][0] <= frames[0][0]:
        return "?"
    samples = sum(n for _, n in frames[1:])
    return f"{samples / ((frames[-1][0] - frames[0][0]) / 1e9):.1f} Hz"


def summary(d: Decoded) -> str:
    uptimes = [r[0] for r in d.pulse] + [r[0] for r in d.ecg] + [r[0] for r in d.acc] + [n[0] for n in d.notes]
    span = (max(uptimes) - min(uptimes)) / 1000 if uptimes else 0
    lines = [
        f"span       {span / 60:.1f} min of board time",
        f"pulse      {len(d.pulse)} readings, {len(d.rr)} beat intervals",
        f"ECG        {len(d.ecg)} samples, {measured_rate(d.frame_times.get(ECG, []))} by the strap's clock",
        f"accel      {len(d.acc)} samples, {measured_rate(d.frame_times.get(ACC, []))} by the strap's clock",
        f"notes      {len(d.notes)}",
    ]
    if d.lost:
        lines.append(f"LOST       {d.lost} records: the board fell behind")
    for key, count in d.unknown_frames.items():
        lines.append(f"SKIPPED    {count} frames of {key}: not decoded yet (still in strap.bin)")
    if d.cut_short:
        lines.append("last record cut short (power cut?)")
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    if len(argv) not in (2, 3):
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    source = Path(argv[1])
    out_dir = Path(argv[2]) if len(argv) == 3 else source.parent
    out_dir.mkdir(parents=True, exist_ok=True)
    d = decode(source.read_bytes())
    write_csv(out_dir / "pulse.csv", ["uptime_ms", "bpm", "contact"], d.pulse)
    write_csv(out_dir / "rr.csv", ["uptime_ms", "rr_ms"], [(u, f"{rr:.1f}") for u, rr in d.rr])
    write_csv(out_dir / "ecg.csv", ["uptime_ms", "strap_ns", "microvolts"], d.ecg)
    write_csv(out_dir / "acc.csv", ["uptime_ms", "strap_ns", "x_mg", "y_mg", "z_mg"], d.acc)
    write_csv(out_dir / "notes.csv", ["uptime_ms", "kind", "text"], d.notes)
    print(summary(d))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
