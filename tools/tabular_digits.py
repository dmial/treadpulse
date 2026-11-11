#!/usr/bin/env python3
"""Makes the digits of lv_font_conv fonts tabular: all ten get the advance of
the widest one and are centred in it, so a changing number does not jiggle.

    tools/tabular_digits.py lib/screen/fonts/geo_50.c ...
"""
import re
import sys
from pathlib import Path

GLYPH = re.compile(
    r"\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), "
    r"\.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}"
)

for path in map(Path, sys.argv[1:]):
    src = path.read_text()
    # Glyph ids follow the order of the bitmaps; id 0 is reserved.
    codes = [int(c, 16) for c in re.findall(r"/\* U\+([0-9A-F]+) ", src)]
    glyphs = list(GLYPH.finditer(src))
    if len(glyphs) != len(codes) + 1:
        sys.exit(f"{path}: {len(codes)} bitmaps but {len(glyphs) - 1} glyph descriptions")
    digit_ids = [i + 1 for i, c in enumerate(codes) if 0x30 <= c <= 0x39]
    if not digit_ids:
        continue
    widest = max(int(glyphs[i].group(2)) for i in digit_ids)  # 1/16 px

    out, last = [], 0
    for i in digit_ids:
        m = glyphs[i]
        adv = int(m.group(2))
        shift = round((widest - adv) / 16 / 2)
        out.append(src[last : m.start()])
        out.append(
            f"{{.bitmap_index = {m.group(1)}, .adv_w = {widest}, .box_w = {m.group(3)}, "
            f".box_h = {m.group(4)}, .ofs_x = {int(m.group(5)) + shift}, .ofs_y = {m.group(6)}}}"
        )
        last = m.end()
    out.append(src[last:])
    path.write_text("".join(out))
    print(f"{path}: digits {widest / 16:.1f} px wide")
