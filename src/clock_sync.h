// Wall-clock time. The board has no battery-backed clock, so time comes from
// NTP while Wi-Fi is up (see net.h). Logs are written in UTC; the screen
// (clock, day boundaries) uses the local zone from user_config.h.
#pragma once

#include <Arduino.h>
#include <time.h>

namespace clocksync {

// Sets the local time zone (user_config.h). Call once at boot.
void begin();

// Fetches NTP time. Wi-Fi must be connected. Returns true when synced.
bool syncNtp(uint32_t timeoutMs);

bool isSynced();

// Wall-clock time of a past millis() value, or 0 when the clock is not set.
time_t epochAtUptime(uint32_t uptimeMs);

// UTC formatted with strftime, "" when the clock is not set.
String formatUtc(time_t t, const char* fmt = "%Y-%m-%dT%H:%M:%SZ");

// Local time formatted with strftime, "" when the clock is not set.
String formatLocal(time_t t, const char* fmt);

}  // namespace clocksync
