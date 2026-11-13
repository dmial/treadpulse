// Treadmill display: shows walking time, speed and incline from the treadmill's
// Bluetooth FTMS data and the pulse from a heart rate strap, logs every walk to
// the board's own flash and uploads finished walks to the cloud over Wi-Fi.
// (The screen-demo firmware has its own setup() and loop(): demo_main.cpp.)
#ifndef UI_DEMO

#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "archive.h"
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
#include "stats.h"
#include "storage.h"
#include "touch.h"
#include "treadmill_link.h"
#include "uploader.h"
#if __has_include("user_config.h")
#include "user_config.h"
#else
#error "src/user_config.h is missing: copy src/user_config.example.h to src/user_config.h and fill it in"
#endif
#include "walk_history.h"
#include "walk_settings.h"
#include "walk_tracker.h"
// Walks from before the board kept a history, made from their cloud copies by
// env:backfill (docs/how-it-works.md, History). Personal data: out of git.
#if __has_include("history_seed.h")
#include "history_seed.h"
#define HAS_HISTORY_SEED 1
#endif

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
// A walk ends seconds after stop is pressed, so Wi-Fi waits this long: a walk
// started again right away is not held up by the upload.
constexpr uint32_t kNetAfterWalkMs = 60UL * 1000;
// Without a clock the day ring cannot tell today's saved walks from older ones
// and shows only this boot's, so a missing clock is retried sooner.
constexpr uint32_t kClockRetryMs = 60UL * 1000;
constexpr uint32_t kClockResyncMs = 24UL * 60 * 60 * 1000;  // daily drift correction
// The walk in progress is saved to the day log this often: a power cut loses
// at most this much of the daily total.
constexpr uint32_t kDaySaveMs = 60 * 1000;

TreadmillLink treadmill;
HeartRateLink heartRate;
walk::WalkTracker tracker(walkSettings());
walk::History history;  // the current (or last kept) walk, for the chart
// The last kept walk's chart, while the new walk may still prove too short to keep.
walk::History previousHistory;
day::DayLog dayLog(userconfig::kDayResetHour);
pulse::LineWatcher pulseLines(userconfig::kPulseCeilingBpm, userconfig::kPulseIntervalBpm);
Storage storage;
screen::Dashboard dash(screenSettings());
Beeper beeper;

uint32_t nextTickMs = 0;
uint32_t lastNetMs = 0;
uint32_t lastClockSyncMs = 0;
uint32_t lastDaySaveMs = 0;
// A walk just ended: go online at netDueMs without waiting for the retry.
bool netNow = false;
uint32_t netDueMs = 0;
uint32_t lastStopCount = 0;  // the treadmill's stop reports already passed on
uint32_t keptWalkSeconds = 0;  // the last kept walk's time, shown between walks
uint32_t keptWalkSteps = 0;
// Kept walks that ended before the clock was set, waiting for a date.
std::vector<walk::Summary> undatedHistory;
// The history's last weeks for the statistics page, and when it began.
std::vector<archive::Entry> recentWalks;
int64_t historyStart = 0;
// Every day of the history, for the days in a row on goal.
stats::DayTotals dayTotals(userconfig::kDayResetHour);
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
day::Walk dayWalk(uint32_t startMs, uint32_t endMs, uint32_t walkSeconds, uint32_t steps) {
    day::Walk w;
    w.thisBoot = true;
    w.startUptimeMs = startMs;
    w.endUptimeMs = endMs;
    w.startEpoch = clocksync::epochAtUptime(startMs);
    w.endEpoch = clocksync::epochAtUptime(endMs);
    w.walkSeconds = walkSeconds;
    w.steps = steps;
    return w;
}

void saveDay(const day::Walk* live) { storage.writeDayLog(String(dayLog.toCsv(live).c_str())); }

// Keeps a walk for the statistics page; walks older than its days are dropped.
void rememberWalk(const archive::Entry& e) {
    if (historyStart == 0 || e.startEpoch < historyStart) historyStart = e.startEpoch;
    dayTotals.add(e);
    recentWalks.push_back(e);
    int64_t newest = 0;
    for (const archive::Entry& w : recentWalks) newest = std::max(newest, w.startEpoch);
    const int64_t oldest = newest - static_cast<int64_t>(stats::kDays + 1) * 24 * 3600;
    recentWalks.erase(std::remove_if(recentWalks.begin(), recentWalks.end(),
                                     [oldest](const archive::Entry& w) { return w.startEpoch < oldest; }),
                      recentWalks.end());
}

void loadHistory() {
    storage.forEachHistoryLine([](const std::string& line) {
        if (auto e = archive::parseEntry(line)) rememberWalk(*e);
    });
    Serial.printf("[history] %u walks of the last %d days in memory\n",
                  static_cast<unsigned>(recentWalks.size()), stats::kDays);
}

// A kept walk goes to the history once it has a date: right away with a clock,
// otherwise when the clock gets set (its uptime still dates it then). Without a
// clock until the power goes, it is left out, as it is from the day ring.
void addToHistory(const walk::Summary& sum) {
    const int64_t start = clocksync::epochAtUptime(sum.startMs);
    const int64_t end = clocksync::epochAtUptime(sum.endMs);
    if (start > 0 && end > 0) {
        const archive::Entry e = archive::fromSummary(sum, start, end, dailyGoalSeconds());
        storage.appendHistory(e);
        rememberWalk(e);
    } else {
        undatedHistory.push_back(sum);
    }
}

void dateHistory() {
    if (undatedHistory.empty() || !clocksync::isSynced()) return;
    std::vector<walk::Summary> walks;
    walks.swap(undatedHistory);
    for (const walk::Summary& sum : walks) addToHistory(sum);
}

// Adds the seed's walks the history lacks and writes the goal into walks kept
// before the history had it (the goal then was the goal now), so a later goal
// never judges them; the next boots find both done.
void tidyHistory() {
    std::vector<archive::Entry> entries = archive::parseAll(storage.readHistory().c_str());
    int added = 0;
#ifdef HAS_HISTORY_SEED
    added = archive::merge(entries, archive::parseAll(historyseed::kCsv));
#endif
    const int goals = archive::fillGoals(entries, dailyGoalSeconds());
    if (added > 0 || goals > 0) storage.writeHistory(String(archive::formatAll(entries).c_str()));
    Serial.printf("[history] %d walks added from the seed, %d given the goal, %u in all\n", added, goals,
                  static_cast<unsigned>(entries.size()));
}

// The ring's format, as last chosen by a tap, kept in NVS across power cuts.
bool loadTodayInHours() {
    Preferences prefs;
    prefs.begin("screen", false);
    const bool on = prefs.getBool("todayHours", false);
    prefs.end();
    return on;
}

void saveTodayInHours(bool on) {
    Preferences prefs;
    prefs.begin("screen", false);
    prefs.putBool("todayHours", on);
    prefs.end();
}

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
    if (netNow) {
        if (static_cast<int32_t>(now - netDueMs) < 0) return;
    } else {
        const uint32_t retryMs = clocksync::isSynced() ? kNetRetryMs : kClockRetryMs;
        if (now - lastNetMs < retryMs) return;
    }
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
    sample.treadmillStopped = snap.stopCount != lastStopCount;
    lastStopCount = snap.stopCount;
    if (sample.treadmillStopped) Serial.println("[walk] the treadmill reported a stop");

    const walk::Events ev = tracker.tick(sample);

    if (ev.sessionStarted) {
        storage.startWalk(tracker.sessionStartMs());
        std::swap(history, previousHistory);
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
        row.steps = tracker.steps();
        storage.appendSample(row);
    }
    if (tracker.inSession()) {
        history.push(heartRateUp ? hr.bpm : 0, sample.speedKmh, sample.inclinePct);
    }

    // The day: finished walks go to the day log; the one in progress counts live.
    const int64_t epoch = nowEpoch();
    const day::Walk live =
        dayWalk(tracker.sessionStartMs(), now, tracker.walkSeconds(), tracker.steps());
    const day::Walk* liveWalk = tracker.inSession() ? &live : nullptr;
    if (ev.sessionEnded) {
        const walk::Summary& sum = *ev.sessionEnded;
        if (sum.worthKeeping()) {
            storage.finishWalk(sum);
            addToHistory(sum);
            // The walk ended after the belt stopped (seconds after a reported stop,
            // minutes otherwise); that tail is not walking.
            history.trimTo((sum.endMs - sum.startMs) / 1000 + 1);
            dayLog.add(dayWalk(sum.startMs, sum.endMs, sum.walkSeconds(), sum.steps));
            dayLog.prune(epoch);
            keptWalkSeconds = sum.walkSeconds();
            keptWalkSteps = sum.steps;
            netNow = true;
            netDueMs = now + kNetAfterWalkMs;
        } else {
            // Too short to keep: as if it never happened, the last kept walk
            // comes back on screen.
            storage.discardWalk();
            std::swap(history, previousHistory);
        }
        saveDay(nullptr);  // also drops the walk if it was saved as in progress
    }
    if (dayLog.resolve(epoch, now)) saveDay(liveWalk);  // the clock just got set
    dateHistory();
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
    m.walkSeconds = tracker.inSession() ? tracker.walkSeconds() : keptWalkSeconds;
    m.walkSteps = tracker.inSession() ? tracker.steps() : keptWalkSteps;
    m.walking = tracker.inSession();
    m.pulseUp = heartRateUp;
    m.bpm = hr.bpm;
    m.goalSeconds = dailyGoalSeconds();
    m.today = dayLog.today(epoch, liveWalk);
    m.beltMoving = moving;
    if (epoch > 0) {
        // The walk in progress counts on the statistics page too, as on the ring.
        archive::Entry liveEntry;
        if (tracker.inSession()) {
            liveEntry.startEpoch = clocksync::epochAtUptime(tracker.sessionStartMs());
            liveEntry.walkSeconds = tracker.walkSeconds();
            liveEntry.steps = tracker.steps();
            liveEntry.pulseSeconds = tracker.pulseSeconds();
            liveEntry.zoneSeconds = tracker.zoneSeconds();
            liveEntry.goalSeconds = dailyGoalSeconds();
        }
        const archive::Entry* live = tracker.inSession() ? &liveEntry : nullptr;
        m.hasStats = true;
        m.stats = stats::lastDays(recentWalks, live, historyStart, epoch, userconfig::kDayResetHour,
                                  dailyGoalSeconds());
        m.stats.streakDays = dayTotals.streak(epoch, live, dailyGoalSeconds());
    }
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
    touch::begin();  // before the speaker: both use one pin (board.h)
    dash.build();
    dash.setTodayInHours(loadTodayInHours());
    dash.onTodayFormatChanged(saveTodayInHours);
    showMessage("Starting…");

    storage.begin();
    // Before any new walk can start, close the one a power cut left open.
    storage.recoverUnfinishedWalks();
    clocksync::begin();  // the time zone, which the history's days follow
    tidyHistory();
    loadHistory();
    beeper.begin();
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
