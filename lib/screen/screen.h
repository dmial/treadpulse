// The 800x480 dashboard, drawn with LVGL: the current walk's timer and steps
// above today's ring toward the daily goal (with today's steps inside), speed,
// incline, the pulse with its zone scale and its chart over the walk, and the
// day strip; plus a plain message screen for boot and uploads.
//
// Gray means "no live signal", per source: the treadmill's values and the
// pulse go gray independently.
// No Arduino dependencies: the same code renders on the computer (env:sim).
#pragma once

#include <lvgl.h>

#include <cstdint>
#include <string>
#include <vector>

#include "day_log.h"
#include "pulse_level.h"
#include "pulse_zones.h"
#include "walk_history.h"

namespace screen {

constexpr int kWidth = 800;
constexpr int kHeight = 480;
// The chart gets one column per pixel; right of it stays room for the pulse
// level's number.
constexpr int kChartColumns = 430;
// A fresh walk starts at the left of a 10-minute chart.
constexpr uint32_t kChartMinSpanS = 10 * 60;

enum class Upload { Done, Pending, StorageError };

// "1 walk", "3 walks".
std::string walkCount(int n);

struct Model {
    // Status line
    Upload upload = Upload::Done;
    int pendingWalks = 0;
    std::string clock;  // "14:32", "" while the clock is not set

    // From the treadmill; gray while its link is down
    bool treadmillUp = false;
    float speedKmh = 0.0f;
    float inclinePct = 0.0f;
    uint32_t walkSeconds = 0;  // the current walk, or the last one
    uint32_t walkSteps = 0;    // of the same walk
    bool walking = false;      // a walk is on; otherwise the walk shown is the last one

    // From the chest strap; "---" and gray while there is no fresh pulse
    bool pulseUp = false;
    int bpm = 0;

    // The day
    uint32_t goalSeconds = 0;
    day::Today today;

    // The current walk in kChartColumns columns (empty: nothing to draw)
    std::vector<walk::Column> columns;
    uint32_t chartSpanS = kChartMinSpanS;
};

struct Settings {
    pulse::Borders zones = {};
    float ceilingBpm = 0.0f;
    float intervalBpm = 0.0f;
    const char* ceilingName = "";   // e.g. "VT1", shown next to the line on the chart
    const char* intervalName = "";
    uint32_t levelWindowS = 300;  // the pulse corridor covers the walk's last minutes
    int stripStartHour = 6;
    int stripEndHour = 21;
};

class Dashboard {
   public:
    explicit Dashboard(const Settings& s) : s_(s) {}

    // Creates the screens on the default LVGL display. Call once after lv_init().
    void build();
    // Shows the dashboard with these values.
    void update(const Model& m);
    // Shows a full-screen message until the next update().
    void message(const char* title, const char* detail = "");

   private:
    struct Run {  // a stretch of the pulse line in one color
        uint32_t color = 0;
        std::vector<lv_point_precise_t> points;
    };

    void buildStatus();
    void buildRing();
    void buildNumbers();
    void buildChart();
    void buildDay();

    void updateStatus(const Model& m);
    void updateRing(const Model& m);
    void updateNumbers(const Model& m);
    void updateChart(const Model& m);
    void updateAxis(uint32_t spanS);
    void updateLevel(const std::vector<float>& bpm, int end, float secondsPerColumn, bool pulseUp);
    bool placeLevelCaption(const std::vector<float>& bpm);
    void updateDay(const Model& m);
    void updateWalkMinutes();

    static void onDrawChart(lv_event_t* e);
    static void onDrawDay(lv_event_t* e);
    void drawChart(lv_layer_t* layer) const;
    void drawDay(lv_layer_t* layer) const;

    float pulseY(float bpm) const;

    Settings s_;
    lv_obj_t* dash_ = nullptr;
    lv_obj_t* msg_ = nullptr;

    lv_obj_t* status_ = nullptr;
    lv_obj_t* clock_ = nullptr;

    lv_obj_t* lap1_ = nullptr;
    lv_obj_t* lap2_ = nullptr;
    lv_obj_t* todayValue_ = nullptr;
    lv_obj_t* todayCaption_ = nullptr;
    lv_obj_t* todaySteps_ = nullptr;
    lv_obj_t* walkCaption_ = nullptr;
    lv_obj_t* walkValue_ = nullptr;

    lv_obj_t* speed_ = nullptr;
    lv_obj_t* incline_ = nullptr;
    lv_obj_t* pulse_ = nullptr;
    lv_obj_t* pulseCaption_ = nullptr;
    lv_obj_t* zoneSegments_[pulse::kZoneCount] = {};
    lv_obj_t* zoneMarker_ = nullptr;

    lv_obj_t* chart_ = nullptr;
    std::vector<lv_obj_t*> axis_;
    std::vector<lv_area_t> chartMarks_;  // the two lines and their labels: the level caption keeps off them
    lv_obj_t* levelValue_ = nullptr;
    lv_obj_t* levelCaption_ = nullptr;

    lv_obj_t* day_ = nullptr;
    std::vector<lv_obj_t*> walkMinutes_;  // over the strip's walks; made as needed

    lv_obj_t* msgTitle_ = nullptr;
    lv_obj_t* msgDetail_ = nullptr;

    // What the chart and the day strip draw, kept from the last update(); the
    // chart is rebuilt (and redrawn) only when its input changes.
    bool chartBuilt_ = false;
    std::vector<walk::Column> chartColumns_;
    uint32_t chartSpanS_ = 0;
    bool chartPulseUp_ = false;
    std::vector<Run> pulseRuns_;
    lv_point_precise_t pulseEnd_ = {0, 0};
    uint32_t pulseEndColor_ = 0;
    bool hasPulseEnd_ = false;
    pulse::Level level_;  // over columns [levelFirst_, levelEnd_); not valid: nothing drawn
    int levelFirst_ = 0;
    int levelEnd_ = 0;
    bool dayBuilt_ = false;
    day::Today dayData_;
    bool dayTreadmillUp_ = false;
};

}  // namespace screen
