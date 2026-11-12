// Personal settings of the display. Copy this file to src/user_config.h, put in
// your own values and flash the board. src/user_config.h is git-ignored, so your
// heart rate zones stay on your computer.
#pragma once

#include <stdint.h>

namespace userconfig {

// Daily goal: minutes of walking (belt moving) per day. The ring on the screen
// fills up to it, then runs a second, brighter lap.
constexpr uint32_t kDailyGoalMinutes = 120;

// Heart rate zone borders in bpm, ascending: zone N spans borders[N-1]..borders[N].
// Below the first border is rest, not a zone. These values are an example: put in
// your own. Borders from a lab test (CPET) are best; a rough start is
// 50 / 60 / 70 / 80 / 90 / 100 % of your maximum heart rate.
constexpr float kHeartRateZoneBorders[6] = {95.0f, 115.0f, 135.0f, 150.0f, 165.0f, 185.0f};

// The two pulse lines to watch; crossing either one upwards beeps.
// Easy base walks stay under the first ventilatory threshold (VT1): the top of zone 2...
constexpr float kPulseCeilingBpm = kHeartRateZoneBorders[2];
// ...and the hard part of an interval goes above the second one (VT2): the top of zone 4.
constexpr float kPulseIntervalBpm = kHeartRateZoneBorders[4];

// Names of the two lines on the pulse chart, shown with the last whole bpm under
// the line and the zones they separate: "134 · VT1 · zones 2|3".
constexpr const char* kPulseCeilingName = "VT1";   // first ventilatory threshold
constexpr const char* kPulseIntervalName = "VT2";  // second ventilatory threshold

// The pulse chart marks where the pulse has been over the last minutes: a
// corridor of its middle values with the average inside, the number on the right.
constexpr uint32_t kPulseLevelMinutes = 5;

// Steps are estimated from the belt speed (the treadmill doesn't count them):
//   steps per minute = kStepsPerMinuteBase + kStepsPerMinutePerKmh * speed in km/h
// These values were measured on one walker, 187 cm tall, at 3.0-5.0 km/h; incline
// from 1 to 11 % made no difference. Yours will differ: how to measure them is in
// docs/how-it-works.md, "Steps".
constexpr float kStepsPerMinuteBase = 57.0f;
constexpr float kStepsPerMinutePerKmh = 11.4f;

// Local time zone as a POSIX TZ string, for the clock and the day boundaries.
// Examples: "GMT0BST,M3.5.0/1,M10.5.0" (London), "CET-1CEST,M3.5.0,M10.5.0/3"
// (Berlin, Paris), "EST5EDT,M3.2.0,M11.1.0" (New York), "PST8PDT,M3.2.0,M11.1.0"
// (Los Angeles).
constexpr const char* kTimezone = "UTC0";

// Local hour at which the daily total starts over (0 = midnight).
constexpr int kDayResetHour = 0;

// Local hours covered by the day strip at the bottom of the screen.
constexpr int kDayStripStartHour = 6;
constexpr int kDayStripEndHour = 21;

// ---------------------------------------------------------------- checks

constexpr bool zoneBordersAscend() {
    for (int i = 1; i < 6; ++i) {
        if (!(kHeartRateZoneBorders[i - 1] < kHeartRateZoneBorders[i])) return false;
    }
    return true;
}
static_assert(zoneBordersAscend(), "heart rate zone borders must go up");
static_assert(kPulseCeilingBpm < kPulseIntervalBpm, "the ceiling must be below the interval line");
static_assert(kDailyGoalMinutes > 0, "the daily goal must be above zero");
static_assert(kPulseLevelMinutes > 0, "the pulse level needs at least a minute");
static_assert(kStepsPerMinuteBase + kStepsPerMinutePerKmh * 3.0f > 0.0f,
              "the step formula must give steps at walking speed");
static_assert(kDayResetHour >= 0 && kDayResetHour <= 23, "reset hour is 0..23");
static_assert(kDayStripStartHour >= 0 && kDayStripStartHour < kDayStripEndHour &&
                  kDayStripEndHour <= 24,
              "day strip hours are 0..24, start before end");

}  // namespace userconfig
