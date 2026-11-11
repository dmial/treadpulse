// Treadmill display: shows walking time, speed and incline from the treadmill's
// Bluetooth FTMS data and the pulse from a heart rate strap, logs every walk to
// the board's own flash and uploads finished walks to the cloud over Wi-Fi.
// (The screen-demo firmware has its own setup() and loop(): demo_main.cpp.)
#ifndef UI_DEMO

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "beeper.h"
#include "ble_link.h"
#include "clock_sync.h"
#include "day_log.h"
#include "display.h"
#include "heart_rate_link.h"
#include "net.h"
#include "pulse_zones.h"
#include "screen.h"
#include "screen_settings.h"
#include "storage.h"
#include "treadmill_link.h"
#include "uploader.h"
#if __has_include("user_config.h")
#include "user_config.h"
#else
#error "src/user_config.h is missing: copy src/user_config.example.h to src/user_config.h and fill it in"
#endif
#include "walk_history.h"
#include "walk_tracker.h"

namespace {

constexpr uint32_t kTickMs = 1000;   // tracker + log + screen update rate
constexpr uint32_t kFreshMs = 3000;  // data older than this counts as "no link"
constexpr uint32_t kWifiTimeoutMs = 20000;  // two attempts of 10 s (net.h)
constexpr uint32_t kNtpTimeoutMs = 10000;
// At power-on the user is about to walk, so the upload there is kept short;
// whatever is left goes after the walk.
constexpr uint32_t kBootUploadBudgetMs = 30000;
constexpr uint32_t kUploadBudgetMs = 120000;
constexpr uint32_t kNetRetryMs = 10UL * 60 * 1000;          // after a failed attempt
// Without a clock the day ring cannot tell today's saved walks from older ones
// and shows only this boot's, so a missing clock is retried sooner.
constexpr uint32_t kClockRetryMs = 60UL * 1000;
constexpr uint32_t kClockResyncMs = 24UL * 60 * 60 * 1000;  // daily drift correction
// The walk in progress is saved to the day log this often: a power cut loses
// at most this much of the daily total.
constexpr uint32_t kDaySaveMs = 60 * 1000;

TreadmillLink treadmill;
HeartRateLink heartRate;
walk::WalkTracker tracker;
walk::History history;  // the current (or last) walk, for the chart
day::DayLog dayLog(userconfig::kDayResetHour);
pulse::LineWatcher pulseLines(userconfig::kPulseCeilingBpm, userconfig::kPulseIntervalBpm);
Storage storage;
screen::Dashboard dash(screenSettings());
Beeper beeper;

uint32_t nextTickMs = 0;
uint32_t lastNetMs = 0;
uint32_t lastClockSyncMs = 0;
uint32_t lastDaySaveMs = 0;
bool netNow = false;  // a walk just ended: go online without waiting for the retry
bool wasLinkUp = false;
bool wasMoving = false;

bool clockDue(uint32_t now) {
    return !clocksync::isSynced() || now - lastClockSyncMs >= kClockResyncMs;
}

int64_t nowEpoch() { return clocksync::isSynced() ? static_cast<int64_t>(time(nullptr)) : 0; }

// A blocking call follows a message, so it is drawn right away.
void showMessage(const char* title, const char* detail = "") {
    dash.message(title, detail);
    display::refreshNow();
}

// A walk as the day log sees it, from uptime to wall clock (0 without a clock).
day::Walk dayWalk(uint32_t startMs, uint32_t endMs, uint32_t walkSeconds) {
    day::Walk w;
    w.thisBoot = true;
    w.startUptimeMs = startMs;
    w.endUptimeMs = endMs;
    w.startEpoch = clocksync::epochAtUptime(startMs);
    w.endEpoch = clocksync::epochAtUptime(endMs);
    w.walkSeconds = walkSeconds;
    return w;
}

void saveDay(const day::Walk* live) { storage.writeDayLog(String(dayLog.toCsv(live).c_str())); }

// Wi-Fi shares the radio with Bluetooth, so it is switched on only between
// walks: set the clock, upload finished walks, switch off again.
void netSession(uint32_t uploadBudgetMs) {
    lastNetMs = millis();
    netNow = false;
    if (!net::connect(kWifiTimeoutMs)) return;

    if (clockDue(millis()) && clocksync::syncNtp(kNtpTimeoutMs)) lastClockSyncMs = millis();

    if (storage.pendingCount() > 0) {
        showMessage("Uploading walks…", screen::walkCount(storage.pendingCount()).c_str());
        const uploader::Result r = uploader::sendPending(storage, uploadBudgetMs);
        Serial.printf("[upload] %d sent, %d left%s\n", r.sent, storage.pendingCount(),
                      r.failed ? ", will retry" : "");
    }
    net::disconnect();
}

void maybeNetSession(uint32_t now) {
    if (tracker.inSession()) return;
    if (!clockDue(now) && storage.pendingCount() == 0) return;
    const uint32_t retryMs = clocksync::isSynced() ? kNetRetryMs : kClockRetryMs;
    if (!netNow && now - lastNetMs < retryMs) return;
    netSession(kUploadBudgetMs);
}

void tick(uint32_t now) {
    const TreadmillLink::Snapshot snap = treadmill.snapshot();
    const bool linkUp = snap.connected && snap.hasData && now - snap.lastNotifyMs < kFreshMs;
    const HeartRateLink::Snapshot hr = heartRate.snapshot();
    const bool heartRateUp = hr.connected && hr.hasValid && now - hr.lastValidMs < kFreshMs;

    walk::Sample sample;
    sample.uptimeMs = now;
    sample.fresh = linkUp;
    sample.speedKmh = snap.state.speedKmh;
    sample.inclinePct = snap.state.inclinePct;
    sample.treadmillDistanceM = snap.state.totalDistanceM;
    sample.treadmillElapsedS = snap.state.elapsedTimeS;
    if (heartRateUp) sample.heartRateBpm = hr.bpm;

    const walk::Events ev = tracker.tick(sample);

    if (ev.sessionStarted) {
        storage.startWalk(tracker.sessionStartMs());
        history.clear();
        lastDaySaveMs = now;
    }
    for (const walk::Segment& seg : ev.closedSegments) storage.appendSegment(seg);
    if (tracker.inSession() || ev.sessionEnded) {
        walk::SampleRow row;
        row.uptimeMs = now;
        row.activity = tracker.lastActivity();
        row.walkSeconds = tracker.walkSeconds();
        row.speedKmh = sample.speedKmh;
        row.inclinePct = sample.inclinePct;
        row.distanceM = tracker.distanceM();
        row.treadmillDistanceM = sample.treadmillDistanceM;
        row.treadmillElapsedS = sample.treadmillElapsedS;
        row.heartRateBpm = sample.heartRateBpm;
        storage.appendSample(row);
    }
    if (tracker.inSession()) {
        history.push(heartRateUp ? hr.bpm : 0, sample.speedKmh, sample.inclinePct);
    }

    // The day: finished walks go to the day log; the one in progress counts live.
    const int64_t epoch = nowEpoch();
    const day::Walk live = dayWalk(tracker.sessionStartMs(), now, tracker.walkSeconds());
    const day::Walk* liveWalk = tracker.inSession() ? &live : nullptr;
    if (ev.sessionEnded) {
        const walk::Summary& sum = *ev.sessionEnded;
        storage.finishWalk(sum);
        // The walk ended minutes after the belt stopped; that tail is not walking.
        history.trimTo((sum.endMs - sum.startMs) / 1000 + 1);
        dayLog.add(dayWalk(sum.startMs, sum.endMs, sum.walkMs / 1000));
        dayLog.prune(epoch);
        saveDay(nullptr);
        netNow = true;
    }
    if (dayLog.resolve(epoch, now)) saveDay(liveWalk);  // the clock just got set
    if (liveWalk && now - lastDaySaveMs >= kDaySaveMs) {
        saveDay(liveWalk);
        lastDaySaveMs = now;
    }

    const bool moving = tracker.inSession() && tracker.lastActivity() == walk::Activity::Walk;

    screen::Model m;
    m.upload = !storage.ok()                 ? screen::Upload::StorageError
               : storage.pendingCount() > 0 ? screen::Upload::Pending
                                            : screen::Upload::Done;
    m.pendingWalks = storage.pendingCount();
    m.clock = clocksync::formatLocal(time(nullptr), "%H:%M").c_str();
    m.treadmillUp = linkUp;
    m.speedKmh = sample.speedKmh;
    m.inclinePct = sample.inclinePct;
    m.walkSeconds = tracker.walkSeconds();
    m.walking = tracker.inSession();
    m.pulseUp = heartRateUp;
    m.bpm = hr.bpm;
    m.goalSeconds = dailyGoalSeconds();
    m.today = dayLog.today(epoch, liveWalk);
    m.columns = history.columns(screen::kChartColumns, screen::kChartMinSpanS);
    m.chartSpanS = history.spanSeconds(screen::kChartMinSpanS);
    dash.update(m);

    // Pulse lines, only while actually walking with the strap on.
    switch (pulseLines.update(moving && heartRateUp, hr.bpm)) {
        case pulse::LineWatcher::kCeiling:
            beeper.pulseCeiling();
            break;
        case pulse::LineWatcher::kInterval:
            beeper.pulseInterval();
            break;
        default:
            break;
    }

    // Beep only when the link drops while walking, not when the treadmill is
    // switched off after the belt has stopped.
    if (wasLinkUp && !linkUp && wasMoving) beeper.linkLost();
    if (!wasLinkUp && linkUp && tracker.inSession()) beeper.linkRestored();
    wasLinkUp = linkUp;
    wasMoving = moving;
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[boot] treadmill display v4");

    display::begin();
    dash.build();
    showMessage("Starting…");

    storage.begin();
    // Before any new walk can start, close the one a power cut left open.
    storage.recoverUnfinishedWalks();
    beeper.begin();
    clocksync::begin();
    dayLog.loadCsv(storage.readDayLog().c_str());

    showMessage("Connecting to Wi-Fi…");
    netSession(kBootUploadBudgetMs);
    dayLog.prune(nowEpoch());

    treadmill.begin();
    heartRate.begin();
    Serial.printf("[mem] internal %u KB free, PSRAM %u KB free\n",
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    nextTickMs = millis();
}

void loop() {
    BleLink::loopAll();
    display::loop();

    const uint32_t now = millis();
    if (static_cast<int32_t>(now - nextTickMs) < 0) {
        delay(5);
        return;
    }
    nextTickMs += kTickMs;
    // After a long blocking call (Bluetooth connect, Wi-Fi) skip missed ticks
    // instead of replaying them.
    if (static_cast<int32_t>(now - nextTickMs) > static_cast<int32_t>(kTickMs)) {
        nextTickMs = now + kTickMs;
    }

    tick(now);
    maybeNetSession(now);
}

#endif  // UI_DEMO
