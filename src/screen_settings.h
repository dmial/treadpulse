// The dashboard's settings, taken from user_config.h.
#pragma once

#include "screen.h"
#if __has_include("user_config.h")
#include "user_config.h"
#else
#error "src/user_config.h is missing: copy src/user_config.example.h to src/user_config.h and fill it in"
#endif

inline screen::Settings screenSettings() {
    screen::Settings s;
    for (int i = 0; i <= pulse::kZoneCount; ++i) s.zones[i] = userconfig::kHeartRateZoneBorders[i];
    s.ceilingBpm = userconfig::kPulseCeilingBpm;
    s.intervalBpm = userconfig::kPulseIntervalBpm;
    s.ceilingName = userconfig::kPulseCeilingName;
    s.intervalName = userconfig::kPulseIntervalName;
    s.levelWindowS = userconfig::kPulseLevelMinutes * 60;
    s.stripStartHour = userconfig::kDayStripStartHour;
    s.stripEndHour = userconfig::kDayStripEndHour;
    return s;
}

inline uint32_t dailyGoalSeconds() { return userconfig::kDailyGoalMinutes * 60; }
