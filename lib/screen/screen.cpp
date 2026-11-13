#include "screen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

extern "C" {
LV_FONT_DECLARE(geo_16)
LV_FONT_DECLARE(geo_18)
LV_FONT_DECLARE(geo_26)
LV_FONT_DECLARE(geo_44)
LV_FONT_DECLARE(geo_60)
LV_FONT_DECLARE(geo_80)
}

namespace screen {

namespace {

// ---------------------------------------------------------------- colors

constexpr uint32_t kBg = 0x0b0d10;
constexpr uint32_t kRingTrack = 0x1d232b;
constexpr uint32_t kMuted = 0x6b7280;     // captions
constexpr uint32_t kOff = 0x5b616b;       // a value without a live signal
constexpr uint32_t kOffDim = 0x41464d;    // the first lap, gray, once the goal is met
constexpr uint32_t kOffBand = 0x6b7280;   // zone bands without a pulse
constexpr uint32_t kWhite = 0xf5f5f5;
constexpr uint32_t kFirstLap = 0x8a9099;  // the first lap once the goal is met
constexpr uint32_t kSpeed = 0x4ade80;
constexpr uint32_t kIncline = 0xa78bfa;
constexpr uint32_t kPending = 0xfdba74;
constexpr uint32_t kError = 0xef4444;
constexpr uint32_t kNow = 0xef4444;  // the "now" line on the day strip, as in calendars
constexpr uint32_t kLineLabel = 0x9ca3af;
constexpr uint32_t kDayTrack = 0x151a20;
constexpr uint32_t kDayPast = 0x232a33;
constexpr uint32_t kUntracked = 0x2a3038;  // days before the history began
constexpr uint32_t kStripeBg = 0x439aa5;   // between the stripes: zone 1 at 65 %, calm but still striped

// Pulse by zone; rest (below zone 1) looks like zone 1. No gray here: gray
// means "no signal".
constexpr uint32_t kZoneColor[pulse::kZoneCount + 1] = {0x67e8f9, 0x67e8f9, 0x60a5fa,
                                                        0xfacc15, 0xfb923c, 0xef4444};
// Zone bands behind the pulse line are faint.
constexpr lv_opa_t kBandOpa[pulse::kZoneCount + 1] = {0, 20, 36, 26, 31, 36};

// ---------------------------------------------------------------- layout

// Status line, top
constexpr int32_t kStatusBaseline = 26;

// The top row: the walk's timer, speed, incline and pulse share one baseline,
// each with its caption underneath.
constexpr int32_t kNumberBaseline = 92, kCaptionBaseline = 116;
constexpr int32_t kSpeedX = 320, kInclineX = 438, kPulseX = 594;

// Left column: the walk's timer (the one big number), today's ring below it,
// centred on the same line as the pulse chart.
constexpr int32_t kLeftX = 150;  // centre of the column
constexpr int32_t kLeftW = 296;  // the timer drops to the numbers' size past this
constexpr int32_t kRingY = 258;  // centre
constexpr int32_t kRingOuter = 102, kRingWidth = 14;  // room for "of 120 min today" inside
// Inside the ring: today's minutes, the goal under them, today's steps last.
constexpr int32_t kTodayBaseline = 252, kTodayCaptionBaseline = 274, kTodayStepsBaseline = 298;

// Zone scale under the pulse
constexpr int32_t kScaleY = 128, kSegW = 32, kSegGap = 2, kSegH = 8;
constexpr int32_t kScaleW = pulse::kZoneCount * kSegW + (pulse::kZoneCount - 1) * kSegGap;

// Pulse chart of the walk
constexpr int32_t kChartX = 320, kChartTop = 150, kChartBottom = 366;
constexpr int32_t kChartPad = 6;  // room for round line caps at the edges
constexpr int32_t kPulseTop = 156, kPulseBottom = 360;
constexpr float kPulseLo = 90.0f, kPulseHi = 180.0f;  // bpm at the bottom and top (HRmax 181)
// Just under the lowest the pulse line goes (kPulseBottom), so the minutes
// read as the chart's and not the day strip's.
constexpr int32_t kAxisBaseline = 377;
// The pulse level over the last minutes: a faint corridor, the average inside
// it, its number right of the chart and a caption beside the corridor.
constexpr lv_opa_t kLevelBandOpa = 34;
constexpr int32_t kLevelGap = 6;      // chart edge to the number
constexpr int32_t kLevelDigitH = 13;  // geo_18 digits: the number is centred on the average
constexpr int32_t kCaptionCap = 12;   // geo_16 letters above the baseline

// The level's number: right after the walk's end, past the dot there, but no
// further than just right of the chart.
int32_t levelNumberX(int endColumn) {
    return std::min(kChartX + endColumn + 6, kChartX + screen::kChartColumns + kLevelGap);
}

// Day strip, bottom: each walk's minutes over it and the hours under it, the
// same gap away (geo_16 digits stand kCaptionCap px tall on their baseline).
constexpr int32_t kDayX = 40, kDayW = 720, kDayBarY = 431, kDayBarH = 18;
constexpr int32_t kDayTextGap = 12;
constexpr int32_t kWalkMinutesBaseline = kDayBarY - 1 - kDayTextGap;
constexpr int32_t kHourBaseline = kDayBarY + kDayBarH - 1 + kDayTextGap + kCaptionCap;
constexpr int32_t kWalkMinutesGap = 6;  // least room between two walks' minutes

// Statistics page: the totals over the days on top (the average day, the days
// on goal, minutes in zones 2 to 5), then a bar per day, weeks set apart.
constexpr int32_t kStatsNumberBaseline = 104;
constexpr int32_t kAverageX = 24, kGoalX = 200;  // the days on goal move right past a long average
constexpr int32_t kStatsBlockGap = 24;
constexpr int32_t kZonesX = 470, kZoneColW = 80, kZoneLineW = 62, kZoneLineH = 4;
constexpr int32_t kZonesCaptionBaseline = 62, kZoneMinutesBaseline = 100, kZoneLineY = 108;
constexpr int32_t kZoneNameBaseline = 130;
constexpr int32_t kBarsTop = 168, kBarsBase = 404;  // the longest day reaches kBarsTop
constexpr int32_t kSlotX = 30, kSlotW = 49, kWeekGap = 20, kBarW = 32;
constexpr int32_t kBarMinutesGap = 8;  // a bar's top to its minutes' baseline
constexpr int32_t kDayNumberBaseline = kBarsBase + 26, kWeekdayBaseline = kBarsBase + 48;
constexpr int32_t kTodayLineY = kBarsBase + 55;
constexpr int32_t kDividerTop = 150, kDividerBottom = kBarsBase + 52;
// Room over the longest day; the goal line keeps its place until a day goes higher.
constexpr uint32_t kBarsMinScaleS = 160 * 60, kBarsHeadroomS = 20 * 60;

// Where a moment of the day falls on the strip, clamped to its ends.
int32_t stripX(int sec, int startSec, int endSec) {
    sec = std::min(std::max(sec, startSec), endSec);
    return kDayX + static_cast<int32_t>(int64_t(sec - startSec) * kDayW / (endSec - startSec));
}

// ---------------------------------------------------------------- helpers

lv_color_t rgb(uint32_t c) { return lv_color_hex(c); }

enum class Align { Left, Center, Right };

lv_obj_t* makeBox(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t* makeScreen() {
    lv_obj_t* s = lv_obj_create(nullptr);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s, rgb(kBg), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    return s;
}

// Places a label so its text sits on `baseline`; `x` is its left edge, centre
// or right edge.
void place(lv_obj_t* label, Align align, int32_t x, int32_t baseline, int32_t width) {
    const lv_font_t* f = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label,
                                align == Align::Left     ? LV_TEXT_ALIGN_LEFT
                                : align == Align::Center ? LV_TEXT_ALIGN_CENTER
                                                         : LV_TEXT_ALIGN_RIGHT,
                                0);
    const int32_t left = align == Align::Left ? x : align == Align::Center ? x - width / 2 : x - width;
    lv_obj_set_pos(label, left, baseline - (f->line_height - f->base_line));
}

lv_obj_t* makeLabel(lv_obj_t* parent, const lv_font_t* font, uint32_t color, Align align, int32_t x,
                    int32_t baseline, int32_t width, const char* text = "") {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, rgb(color), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    place(l, align, x, baseline, width);
    lv_label_set_text(l, text);
    return l;
}

int32_t textWidth(const char* text, const lv_font_t* font) {
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

// Setters that leave an unchanged widget alone, so it is not redrawn.
void setText(lv_obj_t* l, const std::string& s) {
    if (std::strcmp(lv_label_get_text(l), s.c_str()) != 0) lv_label_set_text(l, s.c_str());
}

void setTextColor(lv_obj_t* l, uint32_t c) {
    if (!lv_color_eq(lv_obj_get_style_text_color(l, LV_PART_MAIN), rgb(c))) {
        lv_obj_set_style_text_color(l, rgb(c), 0);
    }
}

void setBg(lv_obj_t* o, uint32_t c, lv_opa_t opa) {
    if (!lv_color_eq(lv_obj_get_style_bg_color(o, LV_PART_MAIN), rgb(c))) {
        lv_obj_set_style_bg_color(o, rgb(c), 0);
    }
    if (lv_obj_get_style_bg_opa(o, LV_PART_MAIN) != opa) lv_obj_set_style_bg_opa(o, opa, 0);
}

void setLap(lv_obj_t* arc, int32_t value, uint32_t color) {
    lv_arc_set_value(arc, value);
    const lv_opa_t opa = value > 0 ? LV_OPA_COVER : LV_OPA_TRANSP;
    if (lv_obj_get_style_arc_opa(arc, LV_PART_INDICATOR) != opa) {
        lv_obj_set_style_arc_opa(arc, opa, LV_PART_INDICATOR);
    }
    if (!lv_color_eq(lv_obj_get_style_arc_color(arc, LV_PART_INDICATOR), rgb(color))) {
        lv_obj_set_style_arc_color(arc, rgb(color), LV_PART_INDICATOR);
    }
}

void setVisible(lv_obj_t* o, bool visible) {
    if (visible == lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
        if (visible) {
            lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

std::string walkClock(uint32_t s) {
    char buf[16];
    if (s < 3600) {
        snprintf(buf, sizeof(buf), "%lu:%02lu", static_cast<unsigned long>(s / 60),
                 static_cast<unsigned long>(s % 60));
    } else {
        snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu", static_cast<unsigned long>(s / 3600),
                 static_cast<unsigned long>(s / 60 % 60), static_cast<unsigned long>(s % 60));
    }
    return buf;
}

// 45 -> "45m", 83 -> "1h 23m", 125 -> "2h 05m"; a round hour may drop its
// minutes (120 -> "2h"). No colon: that is the walk's timer.
std::string hoursMinutes(uint32_t minutes, bool dropZeroMinutes = false) {
    const unsigned long h = minutes / 60, m = minutes % 60;
    char buf[24];
    if (h == 0) {
        snprintf(buf, sizeof(buf), "%lum", m);
    } else if (m == 0 && dropZeroMinutes) {
        snprintf(buf, sizeof(buf), "%luh", h);
    } else {
        snprintf(buf, sizeof(buf), "%luh %02lum", h, m);
    }
    return buf;
}

// 8715 -> "8,715 steps".
std::string steps(uint32_t n) {
    std::string digits = std::to_string(n);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) digits.insert(i, ",");
    return digits + (n == 1 ? " step" : " steps");
}

std::string oneDecimal(float v) {
    if (std::fabs(v) < 0.05f) v = 0.0f;  // no "-0.0"
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", v);
    return buf;
}

// The chart draws only the pulse, so only the pulse decides a redraw.
bool samePulse(const std::vector<walk::Column>& a, const std::vector<walk::Column>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].hasData != b[i].hasData || a[i].bpm != b[i].bpm) return false;
    }
    return true;
}

// The day strip moves less than a pixel a minute and shows whole minutes, so it
// is compared by minutes.
bool sameStrip(const day::Today& a, const day::Today& b) {
    if (a.nowSec / 60 != b.nowSec / 60 || a.segments.size() != b.segments.size()) return false;
    for (size_t i = 0; i < a.segments.size(); ++i) {
        if (a.segments[i].startSec / 60 != b.segments[i].startSec / 60 ||
            a.segments[i].endSec / 60 != b.segments[i].endSec / 60 ||
            a.segments[i].walkSeconds / 60 != b.segments[i].walkSeconds / 60) {
            return false;
        }
    }
    return true;
}

lv_point_precise_t point(float x, float y) {
    lv_point_precise_t p;
    p.x = static_cast<lv_value_precise_t>(std::lround(x));
    p.y = static_cast<lv_value_precise_t>(std::lround(y));
    return p;
}

void drawRect(lv_layer_t* layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color,
              lv_opa_t opa = LV_OPA_COVER, int32_t radius = 0) {
    if (x2 < x1 || y2 < y1) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = rgb(color);
    d.bg_opa = opa;
    d.radius = radius;
    lv_area_t a = {x1, y1, x2, y2};
    lv_draw_rect(layer, &d, &a);
}

void drawPolyline(lv_layer_t* layer, const std::vector<lv_point_precise_t>& pts, uint32_t color,
                  int32_t width) {
    if (pts.size() < 2) return;
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = rgb(color);
    d.width = width;
    d.opa = LV_OPA_COVER;
    d.round_start = 1;
    d.round_end = 1;
    d.points = const_cast<lv_point_precise_t*>(pts.data());  // LVGL copies them
    d.point_cnt = static_cast<int32_t>(pts.size());
    lv_draw_line(layer, &d);
}

// Walking without the strap: diagonal stripes in zone 1's color, "pulse not
// measured, most likely easy". One 6x6 tile, repeated over the area.
constexpr int kTile = 6;
uint16_t stripePixels[kTile * kTile];
lv_image_dsc_t stripeTile;

void makeStripeTile() {
    const uint16_t on = lv_color_to_u16(rgb(kZoneColor[1])), off = lv_color_to_u16(rgb(kStripeBg));
    for (int y = 0; y < kTile; ++y) {
        for (int x = 0; x < kTile; ++x) stripePixels[y * kTile + x] = (x + y) % kTile < 2 ? on : off;
    }
    stripeTile.header.magic = LV_IMAGE_HEADER_MAGIC;
    stripeTile.header.cf = LV_COLOR_FORMAT_RGB565;
    stripeTile.header.w = kTile;
    stripeTile.header.h = kTile;
    stripeTile.header.stride = kTile * sizeof(uint16_t);
    stripeTile.data_size = sizeof(stripePixels);
    stripeTile.data = reinterpret_cast<const uint8_t*>(stripePixels);
}

void drawStripes(lv_layer_t* layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    if (x2 < x1 || y2 < y1) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = rgb(kStripeBg);
    d.bg_opa = LV_OPA_COVER;
    d.bg_image_src = &stripeTile;
    d.bg_image_opa = LV_OPA_COVER;
    d.bg_image_tiled = 1;
    lv_area_t a = {x1, y1, x2, y2};
    // LVGL draws the tiles whole, past the area's right and bottom edges, so the
    // layer's clip area is narrowed to the area for this one rectangle.
    const lv_area_t clip = layer->_clip_area;
    const lv_area_t inside = {std::max(clip.x1, a.x1), std::max(clip.y1, a.y1),
                              std::min(clip.x2, a.x2), std::min(clip.y2, a.y2)};
    if (inside.x2 < inside.x1 || inside.y2 < inside.y1) return;
    layer->_clip_area = inside;
    lv_draw_rect(layer, &d, &a);
    layer->_clip_area = clip;
}

// Whole minutes a day over `days`, rounded: a minute or two in a hard zone
// says enough, tenths would add nothing.
uint32_t minutesADay(uint32_t totalSeconds, int days) {
    if (days <= 0) return 0;
    return static_cast<uint32_t>(std::lround(totalSeconds / 60.0 / days));
}

// A zone's minutes inside its part of a day's bar, in the background's color,
// when they fit; a part too small for them is too small to matter.
void drawPartMinutes(lv_layer_t* layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                     uint32_t minutes) {
    constexpr int32_t kPad = 2;
    if (minutes == 0 || y2 - y1 + 1 < kCaptionCap + 2 * kPad) return;
    const std::string text = std::to_string(minutes);
    const int32_t w = textWidth(text.c_str(), &geo_16);
    if (w > x2 - x1 + 1 - 2 * kPad) return;
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = &geo_16;
    d.color = rgb(kBg);
    d.text = text.c_str();
    d.text_local = 1;  // LVGL copies the text
    d.align = LV_TEXT_ALIGN_CENTER;
    // The digits' middle on the part's middle.
    const int32_t baseline = (y1 + y2 + 1) / 2 + kCaptionCap / 2;
    const int32_t top = baseline - (geo_16.line_height - geo_16.base_line);
    lv_area_t a = {x1, top, x2, top + geo_16.line_height - 1};
    lv_draw_label(layer, &d, &a);
}

// A day's bar from the bottom up: walking without the strap, then the pulse
// zones from easy to hard (below zone 1 joins zone 1, as on the dashboard).
struct BarPart {
    uint32_t seconds;
    int zone;  // -1: without the strap
};

std::vector<BarPart> barParts(const stats::Day& d) {
    std::vector<BarPart> parts;
    parts.push_back({d.walkSeconds > d.pulseSeconds ? d.walkSeconds - d.pulseSeconds : 0, -1});
    parts.push_back({d.zoneSeconds[0] + d.zoneSeconds[1], 1});
    for (int z = 2; z <= pulse::kZoneCount; ++z) parts.push_back({d.zoneSeconds[z], z});
    return parts;
}

uint32_t barSeconds(const stats::Day& d) {
    uint32_t total = 0;
    for (const BarPart& p : barParts(d)) total += p.seconds;
    return total;
}

}  // namespace

std::string walkCount(int n) {
    return std::to_string(n) + (n == 1 ? " walk" : " walks");
}

// ---------------------------------------------------------------- build

void Dashboard::build() {
    dash_ = makeScreen();
    buildStatus();
    buildRing();
    buildNumbers();
    buildChart();
    buildDay();

    buildStats();

    msg_ = makeScreen();
    msgTitle_ = makeLabel(msg_, &geo_26, kWhite, Align::Center, kWidth / 2, 232, 760);
    msgDetail_ = makeLabel(msg_, &geo_18, kMuted, Align::Center, kWidth / 2, 270, 760);

    // A sideways swipe anywhere switches between the dashboard and the statistics.
    lv_obj_add_event_cb(dash_, onSwipe, LV_EVENT_GESTURE, this);
    lv_obj_add_event_cb(stats_, onSwipe, LV_EVENT_GESTURE, this);

    lv_screen_load(dash_);
}

void Dashboard::buildStatus() {
    // Upload status on the right: the left corner stays clear above the timer.
    status_ = makeLabel(dash_, &geo_18, kMuted, Align::Right, kWidth - 20, kStatusBaseline, 300);
    clock_ = makeLabel(dash_, &geo_18, kMuted, Align::Center, kWidth / 2, kStatusBaseline, 120);
}

void Dashboard::buildRing() {
    // The walk in progress: the one big number, on the top row's baseline.
    walkValue_ = makeLabel(dash_, &geo_80, kWhite, Align::Center, kLeftX, kNumberBaseline, 300);
    walkCaption_ = makeLabel(dash_, &geo_16, kMuted, Align::Center, kLeftX, kCaptionBaseline, 280,
                             "walk");

    auto makeLapArc = [&](bool withTrack) {
        lv_obj_t* a = lv_arc_create(dash_);
        lv_obj_set_size(a, 2 * kRingOuter, 2 * kRingOuter);
        lv_obj_set_pos(a, kLeftX - kRingOuter, kRingY - kRingOuter);
        lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
        lv_arc_set_rotation(a, 270);  // start at the top
        lv_arc_set_bg_angles(a, 0, 360);
        lv_arc_set_range(a, 0, 1000);
        lv_arc_set_value(a, 0);
        lv_obj_set_style_arc_width(a, kRingWidth, LV_PART_MAIN);
        lv_obj_set_style_arc_color(a, rgb(kRingTrack), LV_PART_MAIN);
        lv_obj_set_style_arc_opa(a, withTrack ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_width(a, kRingWidth, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);
        return a;
    };
    lap1_ = makeLapArc(true);
    lap2_ = makeLapArc(false);  // the second lap, over the first

    todayValue_ = makeLabel(dash_, &geo_44, kWhite, Align::Center, kLeftX, kTodayBaseline, 140);
    todayCaption_ = makeLabel(dash_, &geo_16, kMuted, Align::Center, kLeftX, kTodayCaptionBaseline, 140);
    todaySteps_ = makeLabel(dash_, &geo_18, kLineLabel, Align::Center, kLeftX, kTodayStepsBaseline, 150);

    // Today in hours and minutes: digits as big as the minutes', the letters
    // small and gray, so they read as units. Laid out in drawToday().
    for (int i = 0; i < 4; ++i) {
        const bool unit = i % 2 == 1;
        todayParts_[i] = makeLabel(dash_, unit ? &geo_26 : &geo_44, kWhite, Align::Left, kLeftX,
                                   kTodayBaseline, 10);
        setVisible(todayParts_[i], false);
    }

    // A tap anywhere on the ring switches minutes / hours and minutes. Over
    // everything else in the ring, which takes no taps itself.
    lv_obj_t* tap = lv_obj_create(dash_);
    lv_obj_remove_style_all(tap);
    lv_obj_remove_flag(tap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(tap, 2 * kRingOuter, 2 * kRingOuter);
    lv_obj_set_pos(tap, kLeftX - kRingOuter, kRingY - kRingOuter);
    lv_obj_add_event_cb(tap, onRingTap, LV_EVENT_CLICKED, this);
}

void Dashboard::buildNumbers() {
    speed_ = makeLabel(dash_, &geo_60, kSpeed, Align::Left, kSpeedX, kNumberBaseline, 115);
    makeLabel(dash_, &geo_16, kMuted, Align::Left, kSpeedX + 2, kCaptionBaseline, 115, "km/h");
    incline_ = makeLabel(dash_, &geo_60, kIncline, Align::Left, kInclineX, kNumberBaseline, 150);
    makeLabel(dash_, &geo_16, kMuted, Align::Left, kInclineX + 2, kCaptionBaseline, 150, "% incline");
    pulse_ = makeLabel(dash_, &geo_60, kOff, Align::Left, kPulseX, kNumberBaseline, 200);
    pulseCaption_ = makeLabel(dash_, &geo_16, kMuted, Align::Left, kPulseX + 2, kCaptionBaseline,
                              kWidth - kPulseX - 4);

    for (int i = 0; i < pulse::kZoneCount; ++i) {
        lv_obj_t* seg = makeBox(dash_);
        lv_obj_set_pos(seg, kPulseX + i * (kSegW + kSegGap), kScaleY);
        lv_obj_set_size(seg, kSegW, kSegH);
        lv_obj_set_style_radius(seg, 2, 0);
        zoneSegments_[i] = seg;
    }
    zoneMarker_ = makeBox(dash_);
    lv_obj_set_size(zoneMarker_, 4, kSegH + 10);
    lv_obj_set_style_radius(zoneMarker_, 2, 0);
    lv_obj_set_style_bg_color(zoneMarker_, rgb(kWhite), 0);
    lv_obj_set_style_bg_opa(zoneMarker_, LV_OPA_COVER, 0);
}

void Dashboard::buildChart() {
    chart_ = makeBox(dash_);
    lv_obj_set_pos(chart_, kChartX - kChartPad, kChartTop);
    lv_obj_set_size(chart_, kChartColumns + 2 * kChartPad, kChartBottom - kChartTop);
    lv_obj_add_event_cb(chart_, onDrawChart, LV_EVENT_DRAW_MAIN, this);

    // The two pulse lines to watch, labelled above their dashes with their bpm,
    // name and the zones they separate: "135 · VT1 · zones 2|3".
    const std::pair<float, const char*> lines[] = {{s_.ceilingBpm, s_.ceilingName},
                                                   {s_.intervalBpm, s_.intervalName}};
    for (const auto& [line, name] : lines) {
        std::string text = std::to_string(pulse::lastBpmUnder(line));
        if (name && *name) text += std::string(" · ") + name;
        const int zone = pulse::zoneOf(s_.zones, line);  // the zone the line opens
        if (zone >= 2) text += " · zones " + std::to_string(zone - 1) + "|" + std::to_string(zone);
        const int32_t y = static_cast<int32_t>(pulseY(line));
        makeLabel(dash_, &geo_16, kLineLabel, Align::Left, kChartX + 6, y - 5, kChartColumns - 12,
                  text.c_str());
        chartMarks_.push_back({kChartX, y - 1, kChartX + kChartColumns, y + 1});
        chartMarks_.push_back({kChartX + 6, y - 5 - kCaptionCap - 1,
                               kChartX + 6 + textWidth(text.c_str(), &geo_16), y - 5 + 4});
    }
    for (int i = 0; i < 6; ++i) {
        lv_obj_t* l = makeLabel(dash_, &geo_16, kMuted, Align::Left, kChartX, kAxisBaseline, 90);
        lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
        axis_.push_back(l);
    }

    levelValue_ = makeLabel(dash_, &geo_18, kWhite, Align::Left, kChartX + kChartColumns + kLevelGap,
                            kChartTop, kWidth - kChartX - kChartColumns - kLevelGap);
    lv_obj_add_flag(levelValue_, LV_OBJ_FLAG_HIDDEN);
    const std::string caption = "last " + std::to_string(s_.levelWindowS / 60) + " min";
    levelCaption_ = makeLabel(dash_, &geo_16, kLineLabel, Align::Right, kChartX, kChartTop, 120,
                              caption.c_str());
    lv_obj_add_flag(levelCaption_, LV_OBJ_FLAG_HIDDEN);
}

void Dashboard::buildDay() {
    day_ = makeBox(dash_);
    lv_obj_set_pos(day_, kDayX, kDayBarY);
    lv_obj_set_size(day_, kDayW, kDayBarH);
    lv_obj_add_event_cb(day_, onDrawDay, LV_EVENT_DRAW_MAIN, this);

    const int start = s_.stripStartHour, end = s_.stripEndHour;
    const int span = end - start;
    const int step = span % 3 == 0 ? 3 : span % 2 == 0 ? 2 : 1;
    for (int h = start; h <= end; h += step) {
        const int32_t x = kDayX + (h - start) * kDayW / span;
        const Align a = h == start ? Align::Left : h == end ? Align::Right : Align::Center;
        makeLabel(dash_, &geo_16, kMuted, a, x, kHourBaseline, 50, std::to_string(h).c_str());
    }
}

void Dashboard::buildStats() {
    makeStripeTile();
    stats_ = makeScreen();
    // The status line as on the dashboard, with the days covered on the left.
    statsSince_ = makeLabel(stats_, &geo_18, kMuted, Align::Left, 20, kStatusBaseline, 300);
    statsClock_ = makeLabel(stats_, &geo_18, kMuted, Align::Center, kWidth / 2, kStatusBaseline, 120);
    statsStatus_ = makeLabel(stats_, &geo_18, kMuted, Align::Right, kWidth - 20, kStatusBaseline, 300);
    statsWaiting_ = makeLabel(stats_, &geo_18, kMuted, Align::Center, kWidth / 2, 250, 600,
                              "Waiting for the clock…");

    statsBody_ = makeBox(stats_);
    lv_obj_remove_style_all(statsBody_);
    lv_obj_set_size(statsBody_, kWidth, kHeight);
    lv_obj_t* b = statsBody_;

    average_ = makeLabel(b, &geo_44, kWhite, Align::Left, kAverageX, kStatsNumberBaseline, 150);
    // Two-line captions beside the numbers, ending on their baseline.
    averageCaption_ = makeLabel(b, &geo_16, kMuted, Align::Left, kAverageX, kStatsNumberBaseline, 150,
                                "min a day\non average");
    goalDays_ = makeLabel(b, &geo_44, kWhite, Align::Left, kGoalX, kStatsNumberBaseline, 100);
    goalOf_ = makeLabel(b, &geo_44, kMuted, Align::Left, kGoalX, kStatsNumberBaseline, 120);
    goalCaption_ = makeLabel(b, &geo_16, kMuted, Align::Left, kGoalX, kStatsNumberBaseline, 150,
                             "days\non goal");
    zonesCaption_ = makeLabel(b, &geo_16, kMuted, Align::Left, kZonesX, kZonesCaptionBaseline, 320,
                              "time in pulse zones, a day on average");
    for (int i = 0; i < 4; ++i) {
        const int32_t x = kZonesX + i * kZoneColW;
        zoneMinutes_[i] = makeLabel(b, &geo_26, kOffDim, Align::Left, x, kZoneMinutesBaseline, kZoneColW);
        // "min" after the number, so it does not read as a pulse
        zoneUnits_[i] = makeLabel(b, &geo_16, kOffDim, Align::Left, x, kZoneMinutesBaseline, kZoneColW, "min");
        zoneLines_[i] = makeBox(b);
        lv_obj_set_pos(zoneLines_[i], x, kZoneLineY);
        lv_obj_set_size(zoneLines_[i], kZoneLineW, kZoneLineH);
        lv_obj_set_style_radius(zoneLines_[i], kZoneLineH / 2, 0);
        zoneNames_[i] = makeLabel(b, &geo_16, kOffDim, Align::Left, x, kZoneNameBaseline, kZoneColW,
                                  ("zone " + std::to_string(i + 2)).c_str());
    }
    noFullDay_ = makeLabel(b, &geo_16, kMuted, Align::Left, kAverageX, kZoneMinutesBaseline, 600,
                           "Averages start after the first full day");

    bars_ = makeBox(b);
    lv_obj_set_pos(bars_, 0, kDividerTop);
    lv_obj_set_size(bars_, kWidth, kHeight - kDividerTop);
    lv_obj_add_event_cb(bars_, onDrawStats, LV_EVENT_DRAW_MAIN, this);
    goalLabel_ = makeLabel(b, &geo_16, kLineLabel, Align::Left, 0, kBarsTop, 50);
    for (int i = 0; i < stats::kDays; ++i) {
        lv_obj_t* minutes = makeLabel(b, &geo_16, kWhite, Align::Center, 0, kBarsBase, kSlotW);
        lv_obj_set_style_bg_color(minutes, rgb(kBg), 0);
        lv_obj_set_style_bg_opa(minutes, LV_OPA_COVER, 0);
        barMinutes_.push_back(minutes);
        dayNumbers_.push_back(makeLabel(b, &geo_16, kMuted, Align::Center, 0, kDayNumberBaseline, kSlotW));
        weekdays_.push_back(makeLabel(b, &geo_16, kOffDim, Align::Center, 0, kWeekdayBaseline, kSlotW));
    }
}

// ---------------------------------------------------------------- update

void Dashboard::update(const Model& m) {
    // The belt starting brings the dashboard back from the statistics.
    if (m.beltMoving && !wasMoving_) page_ = Page::Main;
    wasMoving_ = m.beltMoving;
    loadPage();
    updateStatus(m);
    updateRing(m);
    updateNumbers(m);
    updateChart(m);
    updateDay(m);
    updateStats(m);
}

void Dashboard::loadPage() {
    lv_obj_t* page = page_ == Page::Stats ? stats_ : dash_;
    if (lv_screen_active() != page) lv_screen_load(page);
}

void Dashboard::showPage(Page p) {
    page_ = p;
    if (lv_screen_active() != msg_) loadPage();  // a message stays until the next update()
}

void Dashboard::onSwipe(lv_event_t* e) {
    auto* self = static_cast<Dashboard*>(lv_event_get_user_data(e));
    const lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) return;
    self->showPage(self->page_ == Page::Main ? Page::Stats : Page::Main);
}

void Dashboard::message(const char* title, const char* detail) {
    setText(msgTitle_, title);
    setText(msgDetail_, detail);
    if (lv_screen_active() != msg_) lv_screen_load(msg_);
}

void Dashboard::updateStatus(const Model& m) {
    std::string text;
    uint32_t color = kMuted;
    switch (m.upload) {
        case Upload::Done:
            text = "all uploaded";
            break;
        case Upload::Pending:
            text = "to upload: " + std::to_string(m.pendingWalks);
            color = kPending;
            break;
        case Upload::StorageError:
            text = "storage error";
            color = kError;
            break;
    }
    // The same status line on both pages.
    for (lv_obj_t* l : {status_, statsStatus_}) {
        setText(l, text);
        setTextColor(l, color);
    }
    setText(clock_, m.clock);
    setText(statsClock_, m.clock);
}

void Dashboard::updateRing(const Model& m) {
    const bool up = m.treadmillUp;

    const std::string walkName = m.walking ? "this walk" : m.walkSeconds > 0 ? "last walk" : "walk";
    setText(walkCaption_,
            m.walkSeconds > 0 && m.walkSteps > 0 ? walkName + " · " + steps(m.walkSteps) : walkName);
    // A walk past an hour ("1:02:03") does not fit the big font: one size down.
    const std::string walk = walkClock(m.walkSeconds);
    const lv_font_t* walkFont = textWidth(walk.c_str(), &geo_80) <= kLeftW ? &geo_80 : &geo_60;
    if (lv_obj_get_style_text_font(walkValue_, LV_PART_MAIN) != walkFont) {
        lv_obj_set_style_text_font(walkValue_, walkFont, 0);
        place(walkValue_, Align::Center, kLeftX, kNumberBaseline, 300);
    }
    setText(walkValue_, walk);
    setTextColor(walkValue_, up ? kWhite : kOff);

    const uint32_t goal = std::max<uint32_t>(m.goalSeconds, 1);
    const uint32_t total = m.today.totalSeconds;
    const bool met = total >= goal;
    // Past the goal a second lap starts over the first; there is no third.
    const int32_t lap1 = met ? 1000 : static_cast<int32_t>(uint64_t(total) * 1000 / goal);
    const int32_t lap2 =
        met ? static_cast<int32_t>(std::min<uint64_t>(1000, uint64_t(total - goal) * 1000 / goal)) : 0;
    setLap(lap1_, lap1, !up ? (met ? kOffDim : kOff) : (met ? kFirstLap : kWhite));
    setLap(lap2_, lap2, up ? kWhite : kOff);

    today_.totalS = total;
    today_.goalS = goal;
    today_.steps = m.today.totalSteps;
    today_.up = up;
    hasToday_ = true;
    drawToday();
}

void Dashboard::setTodayInHours(bool on) {
    todayInHours_ = on;
    if (hasToday_) drawToday();
}

void Dashboard::onRingTap(lv_event_t* e) {
    // A swipe that started on the ring switches the page, not the ring.
    if (lv_indev_get_gesture_dir(lv_indev_active()) != LV_DIR_NONE) return;
    auto* self = static_cast<Dashboard*>(lv_event_get_user_data(e));
    self->setTodayInHours(!self->todayInHours_);
    if (self->onTodayFormat_) self->onTodayFormat_(self->todayInHours_);
}

// The ring's centre. The day counts in whole minutes, as the walks on the day
// strip do, or in hours and minutes: only the walk's timer has a colon.
void Dashboard::drawToday() {
    const TodayView& t = today_;
    const bool met = t.totalS >= t.goalS;
    const uint32_t minutes = t.totalS / 60;
    const uint32_t digitColor = t.up ? kWhite : kOff;

    if (todayInHours_) {
        setVisible(todayValue_, false);
        placeTodayHours(minutes, digitColor, t.up ? kLineLabel : kOff);
    } else {
        for (lv_obj_t* p : todayParts_) setVisible(p, false);
        setVisible(todayValue_, true);
        setText(todayValue_, std::to_string(minutes));
        setTextColor(todayValue_, digitColor);
    }

    const uint32_t goalMin = t.goalS / 60;
    const uint32_t overMin = met ? (t.totalS - t.goalS) / 60 : 0;
    if (!met) {
        setText(todayCaption_, "of " +
                                   (todayInHours_ ? hoursMinutes(goalMin, true)
                                                  : std::to_string(goalMin) + " min") +
                                   " today");
    } else if (overMin == 0) {
        setText(todayCaption_, "goal reached");
    } else {
        setText(todayCaption_, "+" +
                                   (todayInHours_ ? hoursMinutes(overMin)
                                                  : std::to_string(overMin) + " min") +
                                   " over");
    }
    setTextColor(todayCaption_, met && t.up ? kWhite : kMuted);
    setText(todaySteps_, steps(t.steps));
    setTextColor(todaySteps_, t.up ? kLineLabel : kOff);
}

// "1h 05m" (or "45m" under an hour) centred in the ring, digits and letters on
// one baseline. Laid out again only when the text changes.
void Dashboard::placeTodayHours(uint32_t minutes, uint32_t digitColor, uint32_t unitColor) {
    constexpr int32_t kUnitGap = 2;   // a number to its letter
    constexpr int32_t kPartGap = 10;  // "h" to the minutes
    const uint32_t h = minutes / 60, m = minutes % 60;
    const bool withHours = h > 0;
    char mins[8];
    snprintf(mins, sizeof(mins), withHours ? "%02lu" : "%lu", static_cast<unsigned long>(m));
    const std::string text[4] = {withHours ? std::to_string(h) : "", "h", mins, "m"};

    const std::string key = text[0] + " " + text[2];
    if (key != todayPartsText_) {
        todayPartsText_ = key;
        int32_t w[4];
        for (int i = 0; i < 4; ++i) {
            w[i] = textWidth(text[i].c_str(), lv_obj_get_style_text_font(todayParts_[i], LV_PART_MAIN));
        }
        const int32_t total = (withHours ? w[0] + kUnitGap + w[1] + kPartGap : 0) + w[2] + kUnitGap + w[3];
        int32_t x = kLeftX - total / 2;
        for (int i = withHours ? 0 : 2; i < 4; ++i) {
            setText(todayParts_[i], text[i]);
            place(todayParts_[i], Align::Left, x, kTodayBaseline, w[i] + 4);
            x += w[i] + (i % 2 == 0 ? kUnitGap : kPartGap);
        }
    }
    for (int i = 0; i < 4; ++i) {
        setVisible(todayParts_[i], withHours || i >= 2);
        setTextColor(todayParts_[i], i % 2 == 0 ? digitColor : unitColor);
    }
}

void Dashboard::updateNumbers(const Model& m) {
    const bool up = m.treadmillUp;
    setText(speed_, oneDecimal(m.speedKmh));
    setTextColor(speed_, up ? kSpeed : kOff);
    setText(incline_, oneDecimal(m.inclinePct));
    setTextColor(incline_, up ? kIncline : kOff);

    if (!m.pulseUp || m.bpm <= 0) {
        setText(pulse_, "---");
        setTextColor(pulse_, kOff);
        setText(pulseCaption_, "bpm");
        for (lv_obj_t* seg : zoneSegments_) setBg(seg, kOff, LV_OPA_60);
        setVisible(zoneMarker_, false);
        return;
    }

    const int zone = pulse::zoneOf(s_.zones, static_cast<float>(m.bpm));
    setText(pulse_, std::to_string(m.bpm));
    setTextColor(pulse_, kZoneColor[zone]);

    const pulse::Hint h = pulse::hintFor(m.bpm, s_.ceilingBpm, s_.intervalBpm);
    // Which line is not spelled out: the pulse color and the marker show it.
    char caption[48];
    snprintf(caption, sizeof(caption), "bpm · %d %s threshold", h.distance,
             h.kind == pulse::Hint::Kind::AboveInterval ? "above" : "below");
    setText(pulseCaption_, caption);

    for (int i = 0; i < pulse::kZoneCount; ++i) {
        setBg(zoneSegments_[i], kZoneColor[i + 1], i + 1 == zone ? LV_OPA_COVER : LV_OPA_50);
    }
    const float pos = pulse::scalePosition(s_.zones, static_cast<float>(m.bpm));
    lv_obj_set_pos(zoneMarker_, kPulseX + static_cast<int32_t>(std::lround(pos * kScaleW)) - 2,
                   kScaleY - 5);
    setVisible(zoneMarker_, true);
}

float Dashboard::pulseY(float bpm) const {
    bpm = std::min(std::max(bpm, kPulseLo), kPulseHi);
    return kPulseBottom - (bpm - kPulseLo) * (kPulseBottom - kPulseTop) / (kPulseHi - kPulseLo);
}

void Dashboard::updateChart(const Model& m) {
    if (chartBuilt_ && chartPulseUp_ == m.pulseUp && chartSpanS_ == m.chartSpanS &&
        samePulse(chartColumns_, m.columns)) {
        return;
    }
    chartBuilt_ = true;
    chartColumns_ = m.columns;
    chartSpanS_ = m.chartSpanS;
    chartPulseUp_ = m.pulseUp;
    pulseRuns_.clear();
    hasPulseEnd_ = false;

    const std::vector<walk::Column>& cols = m.columns;
    auto colorOf = [&](float bpm) { return m.pulseUp ? kZoneColor[pulse::zoneOf(s_.zones, bpm)] : kOff; };

    // The pulse smoothed over ~12 s, so beat-to-beat wobble does not turn the
    // line into a saw; gaps (no strap) stay gaps.
    const float secondsPerColumn = static_cast<float>(m.chartSpanS) / kChartColumns;
    const int radius = std::min(4, std::max(1, static_cast<int>(std::lround(12.0f / secondsPerColumn))));
    std::vector<float> bpm(cols.size(), 0.0f);
    for (int i = 0; i < static_cast<int>(cols.size()); ++i) {
        if (!cols[i].hasData || cols[i].bpm <= 0.0f) continue;
        float sum = 0.0f;
        int n = 0;
        for (int j = std::max(0, i - radius); j <= std::min<int>(cols.size() - 1, i + radius); ++j) {
            if (cols[j].hasData && cols[j].bpm > 0.0f) {
                sum += cols[j].bpm;
                ++n;
            }
        }
        bpm[i] = sum / n;
    }

    // A line per stretch of one zone, split exactly where it crosses a border.
    bool newLine = true;
    int prev = -1;
    for (int i = 0; i < static_cast<int>(cols.size()); ++i) {
        if (bpm[i] <= 0.0f) {
            prev = -1;
            newLine = true;
            continue;
        }
        if (prev >= 0) {
            const float x0 = static_cast<float>(kChartX + prev), x1 = static_cast<float>(kChartX + i);
            const float b0 = bpm[prev], b1 = bpm[i];
            std::vector<float> cuts;
            for (float border : s_.zones) {
                if ((border > b0 && border < b1) || (border < b0 && border > b1)) {
                    cuts.push_back((border - b0) / (b1 - b0));
                }
            }
            std::sort(cuts.begin(), cuts.end());
            cuts.push_back(1.0f);
            float t0 = 0.0f;
            for (float t1 : cuts) {
                const float bm = b0 + (b1 - b0) * (t0 + t1) / 2;
                const uint32_t color = colorOf(bm);
                const lv_point_precise_t pa = point(x0 + (x1 - x0) * t0, pulseY(b0 + (b1 - b0) * t0));
                const lv_point_precise_t pb = point(x0 + (x1 - x0) * t1, pulseY(b0 + (b1 - b0) * t1));
                if (newLine || pulseRuns_.empty() || pulseRuns_.back().color != color) {
                    pulseRuns_.push_back(Run{color, {pa}});
                    newLine = false;
                }
                pulseRuns_.back().points.push_back(pb);
                t0 = t1;
            }
        }
        prev = i;
        pulseEnd_ = point(static_cast<float>(kChartX + i), pulseY(bpm[i]));
        pulseEndColor_ = colorOf(bpm[i]);
        hasPulseEnd_ = true;
    }

    int end = 0;  // past the walk's last column
    for (int i = 0; i < static_cast<int>(cols.size()); ++i) {
        if (cols[i].hasData) end = i + 1;
    }
    updateLevel(bpm, end, secondsPerColumn, m.pulseUp);
    updateAxis(m.chartSpanS);
    lv_obj_invalidate(chart_);
}

// The level over the walk's last minutes, from the same smoothed pulse the line
// is drawn with. Only with the strap on, and once at least 40 % of that window
// had a pulse: not from the first few seconds.
void Dashboard::updateLevel(const std::vector<float>& bpm, int end, float secondsPerColumn,
                            bool pulseUp) {
    const float windowColumns = static_cast<float>(s_.levelWindowS) / secondsPerColumn;
    levelEnd_ = end;
    levelFirst_ = std::max(0, end - std::max(1, static_cast<int>(std::lround(windowColumns))));
    const size_t minCount = std::max<size_t>(1, static_cast<size_t>(std::ceil(0.4f * windowColumns)));
    level_ = pulseUp ? pulse::levelOf(bpm, levelFirst_, end, minCount) : pulse::Level{};
    if (!level_.valid) {
        setVisible(levelValue_, false);
        setVisible(levelCaption_, false);
        return;
    }
    setText(levelValue_, std::to_string(std::lround(level_.mean)));
    const int32_t numberX = levelNumberX(end);
    place(levelValue_, Align::Left, numberX,
          static_cast<int32_t>(std::lround(pulseY(level_.mean))) + kLevelDigitH / 2, kWidth - numberX);
    setVisible(levelValue_, true);
    setVisible(levelCaption_, placeLevelCaption(bpm));
}

// "last 5 min" goes just left of where the corridor starts, under it or else
// over it: wherever neither the pulse line nor a line or its label is in the
// way. False: no room, the caption stays hidden.
bool Dashboard::placeLevelCaption(const std::vector<float>& bpm) {
    const int32_t w = textWidth(lv_label_get_text(levelCaption_), &geo_16);
    const int32_t right = kChartX + levelFirst_ - 8;
    const int32_t left = right - w;
    if (left < kChartX) return false;

    const int32_t below = static_cast<int32_t>(std::lround(pulseY(level_.low))) + 6 + kCaptionCap;
    const int32_t above = static_cast<int32_t>(std::lround(pulseY(level_.high))) - 6;
    for (int32_t baseline : {below, above}) {
        const lv_area_t box = {left - 2, baseline - kCaptionCap - 2, right + 2, baseline + 4};
        if (box.y1 < kChartTop || box.y2 > kChartBottom) continue;
        bool clear = true;
        for (const lv_area_t& a : chartMarks_) {
            if (a.x1 <= box.x2 && a.x2 >= box.x1 && a.y1 <= box.y2 && a.y2 >= box.y1) clear = false;
        }
        for (int32_t x = box.x1; clear && x <= box.x2; ++x) {
            const int i = x - kChartX;
            if (i < 0 || i >= static_cast<int>(bpm.size()) || bpm[i] <= 0.0f) continue;
            const float y = pulseY(bpm[i]);
            if (y + 3 >= box.y1 && y - 3 <= box.y2) clear = false;  // the line is 3 px thick
        }
        if (clear) {
            place(levelCaption_, Align::Right, right, baseline, w + 4);
            return true;
        }
    }
    return false;
}

void Dashboard::updateAxis(uint32_t spanS) {
    const float spanMin = std::max<uint32_t>(spanS, 60) / 60.0f;
    int step = 240;
    for (int s : {5, 10, 15, 20, 30, 60, 120, 240}) {
        if (spanMin / s <= 4.0f) {
            step = s;
            break;
        }
    }
    const int32_t right = kChartX + kChartColumns;
    size_t k = 0;
    for (int t = 0; t <= spanMin && k < axis_.size(); t += step, ++k) {
        const bool lastTick = t + step > spanMin;
        std::string text = std::to_string(t);
        if (lastTick) text += " min";
        lv_obj_t* l = axis_[k];
        setText(l, text);
        const int32_t x = kChartX + static_cast<int32_t>(std::lround(t / spanMin * kChartColumns));
        if (t == 0) {
            place(l, Align::Left, kChartX, kAxisBaseline, 90);
        } else {
            // Centred on its tick, but kept inside the chart.
            const int32_t w = textWidth(text.c_str(), &geo_16);
            const int32_t numberW = textWidth(std::to_string(t).c_str(), &geo_16);
            const int32_t leftEdge = std::min(x - numberW / 2, right - w);
            place(l, Align::Left, leftEdge, kAxisBaseline, 90);
        }
        setVisible(l, true);
    }
    for (; k < axis_.size(); ++k) setVisible(axis_[k], false);
}

void Dashboard::updateDay(const Model& m) {
    if (dayBuilt_ && dayTreadmillUp_ == m.treadmillUp && sameStrip(dayData_, m.today)) return;
    dayBuilt_ = true;
    dayData_ = m.today;
    dayTreadmillUp_ = m.treadmillUp;
    lv_obj_invalidate(day_);
    updateWalkMinutes();
}

// Each walk's minutes (belt moving) centred over it, kept inside the strip. A
// walk under a minute gets none; where two would collide the longer walk keeps
// its number.
void Dashboard::updateWalkMinutes() {
    struct Tag {
        uint32_t minutes;
        int32_t left, right;
        std::string text;
    };
    const int start = s_.stripStartHour * 3600, end = s_.stripEndHour * 3600;
    std::vector<Tag> tags;
    for (const day::StripSegment& seg : dayData_.segments) {
        if (seg.endSec <= start || seg.startSec >= end || seg.walkSeconds < 60) continue;
        const int32_t a = stripX(seg.startSec, start, end);
        const int32_t b = std::max(stripX(seg.endSec, start, end), a + 2);
        Tag t;
        t.minutes = seg.walkSeconds / 60;
        t.text = std::to_string(t.minutes);
        const int32_t w = textWidth(t.text.c_str(), &geo_16);
        t.left = std::min(std::max((a + b) / 2 - w / 2, kDayX), kDayX + kDayW - w);
        t.right = t.left + w;
        tags.push_back(t);
    }
    std::stable_sort(tags.begin(), tags.end(),
                     [](const Tag& x, const Tag& y) { return x.minutes > y.minutes; });
    std::vector<const Tag*> shown;
    for (const Tag& t : tags) {
        bool clear = true;
        for (const Tag* o : shown) {
            if (t.left < o->right + kWalkMinutesGap && o->left < t.right + kWalkMinutesGap) clear = false;
        }
        if (clear) shown.push_back(&t);
    }

    while (walkMinutes_.size() < shown.size()) {
        walkMinutes_.push_back(makeLabel(dash_, &geo_16, kWhite, Align::Left, kDayX, kWalkMinutesBaseline, 40));
    }
    for (size_t i = 0; i < walkMinutes_.size(); ++i) {
        lv_obj_t* l = walkMinutes_[i];
        if (i >= shown.size()) {
            setVisible(l, false);
            continue;
        }
        setText(l, shown[i]->text);
        place(l, Align::Left, shown[i]->left, kWalkMinutesBaseline, shown[i]->right - shown[i]->left + 4);
        setTextColor(l, dayTreadmillUp_ ? kWhite : kOff);
        setVisible(l, true);
    }
}

void Dashboard::updateStats(const Model& m) {
    setVisible(statsWaiting_, !m.hasStats);
    setVisible(statsBody_, m.hasStats);
    if (!m.hasStats) {
        setText(statsSince_, "");
        hasStatsData_ = false;
        return;
    }
    const uint32_t goal = std::max<uint32_t>(m.goalSeconds, 1);
    if (hasStatsData_ && m.stats == statsData_ && goal == statsGoalS_) return;
    statsData_ = m.stats;
    statsGoalS_ = goal;
    hasStatsData_ = true;
    layoutStats();
    lv_obj_invalidate(bars_);
}

// Left edge of day i's bar: a gap before each Monday sets the weeks apart.
int32_t Dashboard::slotX(int i) const {
    int weeks = 0;
    for (int j = 1; j <= i; ++j) {
        if (statsData_.days[j].weekday == 0) ++weeks;
    }
    return kSlotX + i * kSlotW + weeks * kWeekGap;
}

void Dashboard::layoutStats() {
    const stats::Window& w = statsData_;
    const int n = static_cast<int>(w.days.size());
    setText(statsSince_, w.firstKey ? "since " + stats::shortDate(w.firstKey)
                                    : "last " + std::to_string(n) + " days");

    // The totals count full days only; before the first one, a line instead.
    const bool totals = w.fullDays > 0;
    setVisible(noFullDay_, !totals);
    for (lv_obj_t* o : {average_, averageCaption_, goalDays_, goalOf_, goalCaption_, zonesCaption_}) {
        setVisible(o, totals);
    }
    for (int i = 0; i < 4; ++i) {
        for (lv_obj_t* o : {zoneMinutes_[i], zoneUnits_[i], zoneLines_[i], zoneNames_[i]}) setVisible(o, totals);
    }

    // "93 min a day on average", "3/7 days on goal": the captions follow the
    // numbers, their second line on the numbers' baseline.
    const int32_t captionBaseline = kStatsNumberBaseline - geo_16.line_height;
    const std::string avg = std::to_string(w.averageSeconds / 60);
    setText(average_, avg);
    const int32_t captionX = kAverageX + textWidth(avg.c_str(), &geo_44) + 10;
    place(averageCaption_, Align::Left, captionX, captionBaseline, 150);
    const int32_t goalX =
        std::max(kGoalX, captionX + textWidth(lv_label_get_text(averageCaption_), &geo_16) + kStatsBlockGap);
    const std::string got = std::to_string(w.goalDays), of = "/" + std::to_string(w.fullDays);
    setText(goalDays_, got);
    place(goalDays_, Align::Left, goalX, kStatsNumberBaseline, 100);
    setText(goalOf_, of);
    const int32_t ofX = goalX + textWidth(got.c_str(), &geo_44) + 2;
    place(goalOf_, Align::Left, ofX, kStatsNumberBaseline, 120);
    place(goalCaption_, Align::Left, ofX + textWidth(of.c_str(), &geo_44) + 10, captionBaseline, 150);

    // Minutes a day in zones 2..5 on average, over the same days as the average
    // day, so they compare with it; in their colors, a zone under half a
    // minute a day in gray.
    for (int i = 0; i < 4; ++i) {
        const int z = i + 2;
        const uint32_t minutes = minutesADay(w.zoneSeconds[z], w.fullDays);
        const bool any = minutes > 0;
        const std::string number = std::to_string(minutes);
        setText(zoneMinutes_[i], number);
        setTextColor(zoneMinutes_[i], any ? kZoneColor[z] : kOffDim);
        place(zoneUnits_[i], Align::Left, kZonesX + i * kZoneColW + textWidth(number.c_str(), &geo_26) + 4,
              kZoneMinutesBaseline, kZoneColW);
        setTextColor(zoneUnits_[i], any ? kMuted : kOffDim);
        setBg(zoneLines_[i], any ? kZoneColor[z] : kOffDim, LV_OPA_COVER);
        setTextColor(zoneNames_[i], any ? kLineLabel : kOffDim);
    }

    uint32_t longest = 0;
    for (const stats::Day& d : w.days) longest = std::max(longest, barSeconds(d));
    barsScaleS_ = std::max(kBarsMinScaleS, longest + kBarsHeadroomS);
    auto barTop = [&](uint32_t seconds) {
        return kBarsBase - static_cast<int32_t>(uint64_t(seconds) * (kBarsBase - kBarsTop) / barsScaleS_);
    };

    const int32_t right = slotX(n - 1) + kBarW;
    setText(goalLabel_, hoursMinutes(statsGoalS_ / 60, true));
    place(goalLabel_, Align::Left, right + 6, barTop(statsGoalS_) + 5, 50);

    static const char kWeekdays[] = "MTWTFSS";
    for (int i = 0; i < stats::kDays; ++i) {
        const bool used = i < n;
        setVisible(dayNumbers_[i], used);
        setVisible(weekdays_[i], used);
        setVisible(barMinutes_[i], used && w.days[i].tracked);
        if (!used) continue;
        const stats::Day& d = w.days[i];
        const bool today = i == n - 1;
        const int32_t cx = slotX(i) + kBarW / 2;
        setText(dayNumbers_[i], std::to_string(d.dayOfMonth));
        setTextColor(dayNumbers_[i], today ? kWhite : d.tracked ? kMuted : kUntracked);
        place(dayNumbers_[i], Align::Center, cx, kDayNumberBaseline, kSlotW);
        setText(weekdays_[i], std::string(1, kWeekdays[d.weekday]));
        setTextColor(weekdays_[i], today ? kLineLabel : d.tracked ? kOffDim : kUntracked);
        place(weekdays_[i], Align::Center, cx, kWeekdayBaseline, kSlotW);
        if (!d.tracked) continue;
        // A day without walking gets a dash, so it does not read as missing.
        const bool walked = d.walkSeconds > 0;
        const std::string minutes = walked ? std::to_string(d.walkSeconds / 60) : "\u2014";
        setText(barMinutes_[i], minutes);
        setTextColor(barMinutes_[i], walked ? kWhite : kMuted);
        // Just as wide as the number: its background breaks the goal line under it.
        place(barMinutes_[i], Align::Center, cx, barTop(barSeconds(d)) - kBarMinutesGap,
              textWidth(minutes.c_str(), &geo_16) + 8);
    }
}

// ---------------------------------------------------------------- drawing

void Dashboard::onDrawChart(lv_event_t* e) {
    static_cast<const Dashboard*>(lv_event_get_user_data(e))->drawChart(lv_event_get_layer(e));
}

void Dashboard::onDrawDay(lv_event_t* e) {
    static_cast<const Dashboard*>(lv_event_get_user_data(e))->drawDay(lv_event_get_layer(e));
}

void Dashboard::onDrawStats(lv_event_t* e) {
    static_cast<const Dashboard*>(lv_event_get_user_data(e))->drawStats(lv_event_get_layer(e));
}

void Dashboard::drawStats(lv_layer_t* layer) const {
    if (!hasStatsData_ || statsData_.days.empty()) return;
    const std::vector<stats::Day>& days = statsData_.days;
    const int n = static_cast<int>(days.size());
    const int32_t left = slotX(0), right = slotX(n - 1) + kBarW;
    auto heightOf = [&](uint32_t seconds) {
        return static_cast<int32_t>(uint64_t(seconds) * (kBarsBase - kBarsTop) / barsScaleS_);
    };

    // A thin line between the weeks, midway through the gap before Monday.
    for (int i = 1; i < n; ++i) {
        if (days[i].weekday != 0) continue;
        const int32_t x = slotX(i) - (kSlotW - kBarW + kWeekGap) / 2;
        drawRect(layer, x, kDividerTop, x + 1, kDividerBottom, kRingTrack);
    }
    drawRect(layer, left, kBarsBase, right - 1, kBarsBase, kRingTrack);

    // The goal, dashed, behind the bars.
    lv_draw_line_dsc_t goal;
    lv_draw_line_dsc_init(&goal);
    goal.color = rgb(kLineLabel);
    goal.width = 2;
    goal.dash_width = 6;
    goal.dash_gap = 6;
    const int32_t goalY = kBarsBase - heightOf(statsGoalS_);
    goal.p1 = {left, goalY};
    goal.p2 = {right, goalY};
    lv_draw_line(layer, &goal);

    for (int i = 0; i < n; ++i) {
        if (!days[i].tracked) continue;
        const int32_t x1 = slotX(i), x2 = x1 + kBarW - 1;
        uint32_t below = 0;
        for (const BarPart& p : barParts(days[i])) {
            if (p.seconds == 0) continue;
            const int32_t bottom = kBarsBase - 1 - heightOf(below);
            below += p.seconds;
            const int32_t top = kBarsBase - heightOf(below);
            if (p.zone < 0) {
                drawStripes(layer, x1, top, x2, bottom);
            } else {
                drawRect(layer, x1, top, x2, bottom, kZoneColor[p.zone]);
                if (p.zone >= 2) drawPartMinutes(layer, x1, top, x2, bottom, p.seconds / 60);
            }
        }
    }

    // Today, underlined as "now" is on the day strip.
    const int32_t t = slotX(n - 1);
    drawRect(layer, t, kTodayLineY, t + kBarW - 1, kTodayLineY + 1, kNow);
}

void Dashboard::drawChart(lv_layer_t* layer) const {
    const int32_t x1 = kChartX, x2 = kChartX + kChartColumns - 1;

    // Zone bands
    for (int z = 1; z <= pulse::kZoneCount; ++z) {
        const float lo = std::max(s_.zones[z - 1], kPulseLo);
        const float hi = std::min(s_.zones[z], kPulseHi);
        if (hi <= lo) continue;
        drawRect(layer, x1, static_cast<int32_t>(std::lround(pulseY(hi))), x2,
                 static_cast<int32_t>(std::lround(pulseY(lo))) - 1,
                 chartPulseUp_ ? kZoneColor[z] : kOffBand, kBandOpa[z]);
    }

    // The two lines to watch
    for (float line : {s_.ceilingBpm, s_.intervalBpm}) {
        lv_draw_line_dsc_t d;
        lv_draw_line_dsc_init(&d);
        d.color = rgb(kWhite);
        d.opa = LV_OPA_50;
        d.width = 1;
        d.dash_width = 4;
        d.dash_gap = 4;
        const int32_t y = static_cast<int32_t>(std::lround(pulseY(line)));
        d.p1 = {x1, y};
        d.p2 = {x2, y};
        lv_draw_line(layer, &d);
    }

    // The pulse level: its corridor up to the walk's end and a tick where it
    // starts, under the line; the average over it, reaching out to its number.
    const int32_t levelX = kChartX + levelFirst_;
    if (level_.valid) {
        const int32_t high = static_cast<int32_t>(std::lround(pulseY(level_.high)));
        const int32_t low = static_cast<int32_t>(std::lround(pulseY(level_.low)));
        drawRect(layer, levelX, high, kChartX + levelEnd_ - 1, low, kWhite, kLevelBandOpa);
        drawRect(layer, levelX, high - 4, levelX, low + 4, kWhite, LV_OPA_50);
    }

    // Pulse
    for (const Run& r : pulseRuns_) drawPolyline(layer, r.points, r.color, 3);
    if (level_.valid) {
        const int32_t y = static_cast<int32_t>(std::lround(pulseY(level_.mean)));
        drawRect(layer, levelX, y - 1, levelNumberX(levelEnd_) - 3, y, kWhite);
    }
    if (hasPulseEnd_) {
        drawRect(layer, pulseEnd_.x - 5, pulseEnd_.y - 5, pulseEnd_.x + 5, pulseEnd_.y + 5, pulseEndColor_,
                 LV_OPA_COVER, LV_RADIUS_CIRCLE);
    }
}

void Dashboard::drawDay(lv_layer_t* layer) const {
    const int32_t y1 = kDayBarY, y2 = kDayBarY + kDayBarH - 1;
    const int32_t radius = kDayBarH / 2;
    drawRect(layer, kDayX, y1, kDayX + kDayW - 1, y2, kDayTrack, LV_OPA_COVER, radius);

    const int start = s_.stripStartHour * 3600, end = s_.stripEndHour * 3600;
    auto xAt = [&](int sec) { return stripX(sec, start, end); };

    const int now = dayData_.nowSec;
    if (now > start) drawRect(layer, kDayX, y1, xAt(now) - 1, y2, kDayPast, LV_OPA_COVER, radius);

    const uint32_t walkColor = dayTreadmillUp_ ? kWhite : kOff;
    for (const day::StripSegment& s : dayData_.segments) {
        if (s.endSec <= start || s.startSec >= end) continue;
        const int32_t a = xAt(s.startSec);
        const int32_t b = std::max(xAt(s.endSec), a + 2);  // even a short walk shows
        drawRect(layer, a, y1, b - 1, y2, walkColor);
    }

    // Now: a thin red line across the strip, not past its edges.
    if (now >= start && now <= end) {
        const int32_t x = std::min(std::max(xAt(now), kDayX + 1), kDayX + kDayW - 2);
        drawRect(layer, x - 1, y1, x, y2, kNow);
    }
}

}  // namespace screen
