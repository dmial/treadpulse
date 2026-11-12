#!/usr/bin/env bash
# Regenerates the LVGL bitmap fonts in lib/screen/fonts/ from Geologica
# (SIL Open Font License 1.1, from Google Fonts).
#
#   tools/make_fonts.sh [path/to/Geologica_Auto-Regular.ttf]
#
# Needs Node.js: lv_font_conv is fetched by npx (or set LV_FONT_CONV to a local copy).
# Text sizes carry ASCII and a few marks; the big number sizes carry digits only.
set -euo pipefail
cd "$(dirname "$0")/.."

FONT="${1:-$HOME/Library/Fonts/Geologica_Auto-Regular.ttf}"
CONV="${LV_FONT_CONV:-npx --yes lv_font_conv@1.5.3}"
OUT=lib/screen/fonts
# ASCII, middle dot, en/em dash, ellipsis
TEXT="0x20-0x7E,0xB7,0x2013,0x2014,0x2026"

gen() {  # name size lv_font_conv-args...
    local name=$1 size=$2
    shift 2
    $CONV --font "$FONT" --size "$size" --bpp 4 --no-compress --format lvgl \
        --lv-font-name "$name" -o "$OUT/$name.c" "$@"
    echo "$OUT/$name.c"
}

mkdir -p "$OUT"
gen geo_16 16 --range "$TEXT"
gen geo_18 18 --range "$TEXT"
gen geo_26 26 --range "$TEXT"
gen geo_44 44 --symbols "0123456789:/"
gen geo_60 60 --symbols "0123456789.:-"
gen geo_80 80 --symbols "0123456789:-"

# Equal-width digits, so changing numbers stay put.
python3 tools/tabular_digits.py "$OUT"/geo_*.c
