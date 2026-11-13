#!/usr/bin/env python3
"""Build the desk stand from case.scad.

Renders the two printed parts (frame.stl, wedge.stl), checks that nothing collides
and that both parts print without supports, and refreshes the 3D model inside viewer.html.

    python3 enclosure/build.py

Needs OpenSCAD 2025 or newer: it uses the Manifold backend.
"""

from __future__ import annotations

import base64
import json
import math
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCAD = HERE / "case.scad"
VIEWER = HERE / "viewer.html"
SCREEN = HERE.parent / "docs" / "images" / "walk.png"

PLA_DENSITY = 1.24  # g/cm³; the walls are thin enough to print solid
PRINTED = ["frame", "wedge"]
SHOWN = ["board_pcb", "board_lcd", "board_metal", "board_white", "board_dark", "speaker", "screws"]
CHECKS = {
    "check_frame_board": "frame against the board",
    "check_wedge_board": "wedge against the board",
    "check_frame_wedge": "frame against the wedge",
    "check_speaker": "speaker against everything",
    "check_usb_plug": "USB-C plug through the frame",
}
TOUCH = 0.01  # mm³; parts that only touch leave a zero-volume overlap

Triangle = tuple[tuple[float, float, float], tuple[float, float, float], tuple[float, float, float]]


def find_openscad() -> str:
    for candidate in (shutil.which("openscad"), "/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD"):
        if candidate and Path(candidate).exists():
            return candidate
    sys.exit("OpenSCAD not found. Install it (macOS: brew install --cask openscad@snapshot) and run again.")


def render(openscad: str, part: str, out: Path) -> str:
    """Render one part of case.scad to `out`; returns OpenSCAD's log."""
    fmt = ["--export-format", "binstl"] if out.suffix == ".stl" else []
    result = subprocess.run(
        [openscad, "--backend=Manifold", "-D", f'part="{part}"', "-o", str(out), *fmt, str(SCAD)],
        capture_output=True, text=True,
    )
    log = result.stdout + result.stderr
    if result.returncode != 0 and "top level object is empty" not in log:
        sys.exit(f"OpenSCAD failed on {part}:\n{log}")
    return log


def read_meta(openscad: str, tmp: Path) -> dict:
    out = tmp / "meta.echo"
    render(openscad, "meta", out)
    meta = {}
    for key, value in re.findall(r'ECHO: "META (\w+) (.*)"', out.read_text()):
        meta[key] = json.loads(value)
    return meta


def triangles(path: Path) -> list[Triangle]:
    data = path.read_bytes()
    count = struct.unpack_from("<I", data, 80)[0]
    tris = []
    for i in range(count):
        v = struct.unpack_from("<12f", data, 84 + i * 50)
        tris.append((v[3:6], v[6:9], v[9:12]))
    return tris


def volume(tris: list[Triangle]) -> float:
    """Enclosed volume in mm³ (signed tetrahedra from the origin)."""
    total = 0.0
    for a, b, c in tris:
        total += (a[0] * (b[1] * c[2] - b[2] * c[1])
                  - a[1] * (b[0] * c[2] - b[2] * c[0])
                  + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6
    return total


def overhang_area(tris: list[Triangle], up: tuple[float, float, float]) -> float:
    """Area in mm² of downward faces steeper than 45° that do not sit on the bed."""
    heights = [sum(p[k] * up[k] for k in range(3)) for t in tris for p in t]
    bed = min(heights)
    area = 0.0
    for a, b, c in tris:
        u = [b[k] - a[k] for k in range(3)]
        v = [c[k] - a[k] for k in range(3)]
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        size = math.sqrt(sum(x * x for x in n))
        if size == 0:
            continue
        facing = sum(n[k] * up[k] for k in range(3)) / size
        low = min(sum(p[k] * up[k] for k in range(3)) for p in (a, b, c))
        if facing < -math.cos(math.radians(45)) - 0.01 and low > bed + 0.3:
            area += size / 2
    return area


def print_up(part: str, tilt: float) -> tuple[float, float, float]:
    """Upward direction on the print bed, in model coordinates."""
    t = math.radians(tilt)
    if part == "frame":  # face down: up is into the case, perpendicular to the screen
        return (0.0, math.cos(t), -math.sin(t))
    return (0.0, 0.0, 1.0)  # wedge: standing as on the desk


def embed(data: dict) -> None:
    html = VIEWER.read_text()
    pattern = r'(<script type="application/json" id="model-data">)(.*?)(</script>)'
    if not re.search(pattern, html, flags=re.S):
        sys.exit("viewer.html has no model-data block")
    payload = json.dumps(data, separators=(",", ":"))
    VIEWER.write_text(re.sub(pattern, lambda m: m.group(1) + payload + m.group(3), html, count=1, flags=re.S))


def main() -> None:
    openscad = find_openscad()
    failed = False
    with tempfile.TemporaryDirectory() as tmp_name:
        tmp = Path(tmp_name)
        meta = read_meta(openscad, tmp)
        parts: dict[str, str] = {}
        weights: dict[str, int] = {}

        for part in PRINTED:
            out = HERE / f"{part}.stl"
            render(openscad, part, out)
            tris = triangles(out)
            grams = volume(tris) / 1000 * PLA_DENSITY
            hang = overhang_area(tris, print_up(part, meta["tilt"]))
            weights[part] = round(grams)
            parts[part] = base64.b64encode(out.read_bytes()).decode()
            print(f"{out.name}: {len(tris)} triangles, about {grams:.0f} g of PLA, "
                  f"{hang:.0f} mm² of overhang past 45° (hole tops and small ledges)")

        for part in SHOWN:
            out = tmp / f"{part}.stl"
            render(openscad, part, out)
            parts[part] = base64.b64encode(out.read_bytes()).decode()

        for part, label in CHECKS.items():
            out = tmp / f"{part}.stl"
            log = render(openscad, part, out)
            overlap = 0.0 if "top level object is empty" in log else abs(volume(triangles(out)))
            ok = overlap < TOUCH
            failed |= not ok
            print(f"{'ok  ' if ok else 'FAIL'} {label}" + ("" if ok else f": {overlap:.2f} mm³ overlap"))

    screen = "data:image/png;base64," + base64.b64encode(SCREEN.read_bytes()).decode()
    embed({"meta": meta, "weights": weights, "parts": parts, "screen": screen})
    print(f"{VIEWER.name}: model refreshed")
    if failed:
        sys.exit("Some parts collide; see FAIL above.")


if __name__ == "__main__":
    main()
