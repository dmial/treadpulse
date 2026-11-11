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
constexpr int32_t kRingOuter = 88, kRingWidth = 14;
constexpr int32_t kTodayBaseline = 265, kTodayCaptionBaseline = 287;

// Zone scale under the pulse
constexpr int32_t kScaleY = 128, kSegW = 32, kSegGap = 2, kSegH = 8;
constexpr int32_t kScaleW = pulse::kZoneCount * kSegW + (pulse::kZoneCount - 1) * kSegGap;

// Pulse chart of the walk
constexpr int32_t kChartX = 320, kChartTop = 150, kChartBottom = 366;
constexpr int32_t kChartPad = 6;  // room for round line caps at the edges
constexpr int32_t kPulseTop = 156, kPulseBottom = 360;
constexpr float kPulseLo = 90.0f, kPulseHi = 180.0f;  // bpm at the bottom and top (HRmax 181)
constexpr int32_t kAxisBaseline = 386;
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

// Day strip, bottom
constexpr int32_t kDayX = 40, kDayW = 720, kDayBarY = 420, kDayBarH = 18;
constexpr int32_t kDayCaptionBaseline = 412, kHourBaseline = 466;

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

std::string hoursMinutes(uint32_t s) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu:%02lu", static_cast<unsigned long>(s / 3600),
             static_cast<unsigned long>(s / 60 % 60));
    return buf;
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

// The day strip moves less than a pixel a minute, so it is compared by minutes.
bool sameStrip(const day::Today& a, const day::Today& b) {
    if (a.nowSec / 60 != b.nowSec / 60 || a.segments.size() != b.segments.size()) return false;
    for (size_t i = 0; i < a.segments.size(); ++i) {
        if (a.segments[i].startSec / 60 != b.segments[i].startSec / 60 ||
            a.segments[i].endSec / 60 != b.segments[i].endSec / 60) {
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

    msg_ = makeScreen();
    msgTitle_ = makeLabel(msg_, &geo_26, kWhite, Align::Center, kWidth / 2, 232, 760);
    msgDetail_ = makeLabel(msg_, &geo_18, kMuted, Align::Center, kWidth / 2, 270, 760);

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
    makeLabel(dash_, &geo_18, kMuted, Align::Left, kDayX, kDayCaptionBaseline, 100, "today");
    daySummary_ = makeLabel(dash_, &geo_18, kWhite, Align::Right, kDayX + kDayW, kDayCaptionBaseline, 400);

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

// ---------------------------------------------------------------- update

void Dashboard::update(const Model& m) {
    if (lv_screen_active() != dash_) lv_screen_load(dash_);
    updateStatus(m);
    updateRing(m);
    updateNumbers(m);
    updateChart(m);
    updateDay(m);
}

void Dashboard::message(const char* title, const char* detail) {
    setText(msgTitle_, title);
    setText(msgDetail_, detail);
    if (lv_screen_active() != msg_) lv_screen_load(msg_);
}

void Dashboard::updateStatus(const Model& m) {
    switch (m.upload) {
        case Upload::Done:
            setText(status_, "all uploaded");
            setTextColor(status_, kMuted);
            break;
        case Upload::Pending:
            setText(status_, "to upload: " + std::to_string(m.pendingWalks));
            setTextColor(status_, kPending);
            break;
        case Upload::StorageError:
            setText(status_, "storage error");
            setTextColor(status_, kError);
            break;
    }
    setText(clock_, m.clock);
}

void Dashboard::updateRing(const Model& m) {
    const bool up = m.treadmillUp;

    setText(walkCaption_, m.walking            ? "this walk"
                          : m.walkSeconds > 0 ? "last walk"
                                              : "walk");
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

    setText(todayValue_, hoursMinutes(total));
    setTextColor(todayValue_, up ? kWhite : kOff);
    if (!met) {
        setText(todayCaption_, "of " + hoursMinutes(goal) + " today");
    } else if (total - goal < 60) {
        setText(todayCaption_, "goal reached");
    } else {
        setText(todayCaption_, "+" + std::to_string((total - goal) / 60) + " min over");
    }
    setTextColor(todayCaption_, met && up ? kWhite : kMuted);
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
    if (!dayBuilt_ || dayTreadmillUp_ != m.treadmillUp || !sameStrip(dayData_, m.today)) {
        dayBuilt_ = true;
        dayData_ = m.today;
        dayTreadmillUp_ = m.treadmillUp;
        lv_obj_invalidate(day_);
    }
    const int n = m.today.walkCount;
    if (n == 0) {
        setText(daySummary_, "no walks yet");
    } else {
        setText(daySummary_, walkCount(n) + " · " + hoursMinutes(m.today.totalSeconds));
    }
    setTextColor(daySummary_, m.treadmillUp ? kWhite : kOff);
}

// ---------------------------------------------------------------- drawing

void Dashboard::onDrawChart(lv_event_t* e) {
    static_cast<const Dashboard*>(lv_event_get_user_data(e))->drawChart(lv_event_get_layer(e));
}

void Dashboard::onDrawDay(lv_event_t* e) {
    static_cast<const Dashboard*>(lv_event_get_user_data(e))->drawDay(lv_event_get_layer(e));
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
    auto xAt = [&](int sec) {
        sec = std::min(std::max(sec, start), end);
        return kDayX + static_cast<int32_t>(int64_t(sec - start) * kDayW / (end - start));
    };

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
