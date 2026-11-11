#include "clock_sync.h"

#if __has_include("user_config.h")
#include "user_config.h"
#else
#error "src/user_config.h is missing: copy src/user_config.example.h to src/user_config.h and fill it in"
#endif

namespace clocksync {

namespace {
// Anything before this is the unset clock (1970) rather than a real date.
constexpr time_t kValidEpoch = 1700000000;  // 2023-11-14

String format(const struct tm& t, const char* fmt) {
    char buf[40];
    strftime(buf, sizeof(buf), fmt, &t);
    return String(buf);
}
}  // namespace

void begin() {
    setenv("TZ", userconfig::kTimezone, 1);
    tzset();
}

bool syncNtp(uint32_t timeoutMs) {
    const uint32_t start = millis();
    configTzTime(userconfig::kTimezone, "pool.ntp.org", "time.google.com");
    while (!isSynced() && millis() - start < timeoutMs) delay(200);
    const bool ok = isSynced();
    Serial.printf("[clock] %s\n", ok ? formatUtc(time(nullptr)).c_str() : "sync failed");
    return ok;
}

bool isSynced() { return time(nullptr) > kValidEpoch; }

time_t epochAtUptime(uint32_t uptimeMs) {
    if (!isSynced()) return 0;
    return time(nullptr) - static_cast<time_t>((millis() - uptimeMs) / 1000);
}

String formatUtc(time_t t, const char* fmt) {
    if (t <= kValidEpoch) return "";
    struct tm utc;
    gmtime_r(&t, &utc);
    return format(utc, fmt);
}

String formatLocal(time_t t, const char* fmt) {
    if (t <= kValidEpoch) return "";
    struct tm local;
    localtime_r(&t, &local);
    return format(local, fmt);
}

}  // namespace clocksync
