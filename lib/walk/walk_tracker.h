// Turns a 1 Hz stream of treadmill observations into walking time, walk
// sessions and constant-speed/incline segments. Pure C++, host-testable.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace walk {

enum class Activity : uint8_t {
    Walk,    // belt moving
    Pause,   // connected, belt stopped
    NoData,  // no fresh data from the treadmill (Bluetooth link down)
};

const char* activityName(Activity a);

// One observation, taken once per second by the main loop.
struct Sample {
    uint32_t uptimeMs = 0;
    bool fresh = false;  // a notification arrived within the freshness window
    float speedKmh = 0.0f;
    float inclinePct = 0.0f;
    // The treadmill's own counters. Both read 0 while it is paused and after a
    // stop; after a pause they come back where they were, after a stop a new
    // workout counts up from 0 again.
    std::optional<uint32_t> treadmillDistanceM;
    std::optional<uint16_t> treadmillElapsedS;
    std::optional<uint16_t> heartRateBpm;  // fresh pulse from a worn strap
    // The treadmill reported since the last sample that stop was pressed (or
    // the safety key pulled): its workout is over, so the walk ends right away.
    bool treadmillStopped = false;
};

// Running pulse statistics. Seconds without a pulse reading are skipped.
struct HeartRateStats {
    uint32_t sum = 0;
    uint32_t count = 0;
    uint16_t max = 0;

    void add(std::optional<uint16_t> bpm);
    void merge(const HeartRateStats& other);
    std::optional<float> average() const;
    std::optional<uint16_t> maximum() const;
};

// A stretch of time with constant activity, speed and incline.
struct Segment {
    Activity activity = Activity::Walk;
    uint32_t startMs = 0;
    uint32_t endMs = 0;
    float speedKmh = 0.0f;
    float inclinePct = 0.0f;
    float distanceM = 0.0f;
    // Pulse varies inside a segment, so it is summarized; empty without a strap.
    std::optional<float> averageHeartRateBpm;
    std::optional<uint16_t> maxHeartRateBpm;

    uint32_t durationS() const { return (endMs - startMs + 500) / 1000; }
};

// Whole seconds of a walking time, rounded to the nearest second: samples
// arrive a few ms late at random, so the total sits right on a whole second,
// and truncating it made the shown timer skip or repeat a second.
inline uint32_t roundedSeconds(uint32_t ms) { return (ms + 500) / 1000; }

// A walk with less walking than this is not kept: an accidental start, or a
// quick try of the treadmill's buttons, leaves no trace.
constexpr uint32_t kMinKeptWalkS = 60;

struct Summary {
    uint32_t startMs = 0;
    uint32_t endMs = 0;  // last moment the belt was moving
    uint32_t walkMs = 0;
    float distanceM = 0.0f;
    float maxSpeedKmh = 0.0f;
    float maxInclinePct = 0.0f;
    float elevationGainM = 0.0f;
    uint32_t segmentCount = 0;
    // Over the seconds the belt was moving and the strap gave a pulse; empty without a strap.
    std::optional<float> averageHeartRateBpm;
    std::optional<uint16_t> maxHeartRateBpm;
    uint32_t steps = 0;  // estimated from the speed (StepFormula)

    uint32_t walkSeconds() const { return roundedSeconds(walkMs); }
    bool worthKeeping() const { return walkSeconds() >= kMinKeptWalkS; }
    float averageSpeedKmh() const {
        return walkMs == 0 ? 0.0f : distanceM / (walkMs / 3600000.0f) / 1000.0f;
    }
};

// The treadmill doesn't count steps, so they are estimated from the belt speed:
// steps per minute grow along a straight line with the speed, measured on the
// walker (user_config.h). All zero: no steps counted.
struct StepFormula {
    float base = 0.0f;    // steps per minute at 0 km/h: where the line starts
    float perKmh = 0.0f;  // extra steps per minute for each km/h

    float perMinute(float speedKmh) const {
        const float v = base + perKmh * speedKmh;
        return v > 0.0f ? v : 0.0f;
    }
    float over(float speedKmh, uint32_t ms) const { return perMinute(speedKmh) * ms / 60000.0f; }
};

struct Events {
    bool sessionStarted = false;
    std::vector<Segment> closedSegments;
    std::optional<Summary> sessionEnded;
};

struct Config {
    uint32_t endAfterIdleMs = 5 * 60 * 1000;  // stop/no data this long ends the walk
    // A reported stop ends the walk once the belt stands still, if that happens
    // within this long of the report (it may come just before the speed drops).
    uint32_t stopReportWindowMs = 5000;
    uint32_t stableMs = 3000;      // a new speed/incline must hold this long to open a segment
    float movingThresholdKmh = 0.05f;
    uint32_t maxTickGapMs = 2000;  // clamp for the time step between samples
    StepFormula steps;
};

class WalkTracker {
   public:
    explicit WalkTracker(Config cfg = Config()) : cfg_(cfg) {}

    Events tick(const Sample& s);

    // Ends the current walk right away, as if the treadmill had then stayed idle
    // long enough. Used to close a walk whose log was cut short by a power loss.
    Events endSession();

    bool inSession() const { return inSession_; }
    // Walking time of the current session, or of the last one after it ended.
    uint32_t walkSeconds() const { return roundedSeconds(walkMs_); }
    float distanceM() const { return distanceM_; }
    // Steps of the current session, or of the last one after it ended.
    uint32_t steps() const { return static_cast<uint32_t>(steps_ + 0.5f); }
    Activity lastActivity() const { return lastActivity_; }
    uint32_t sessionStartMs() const { return sessionStartMs_; }

   private:
    struct Key {
        Activity activity = Activity::Walk;
        int32_t speedDeci = 0;
        int32_t inclineDeci = 0;
        bool operator==(const Key& o) const {
            return activity == o.activity && speedDeci == o.speedDeci &&
                   inclineDeci == o.inclineDeci;
        }
        bool operator!=(const Key& o) const { return !(*this == o); }
    };

    struct OpenSegment {
        Key key;
        uint32_t startMs = 0;
        float startDistanceM = 0.0f;
        HeartRateStats heartRate;
    };

    struct PendingChange {
        Key key;
        uint32_t changeMs = 0;  // first sample that deviated from the open segment
        float changeDistanceM = 0.0f;
        uint32_t sinceMs = 0;   // first sample showing the current candidate key
        HeartRateStats heartRate;  // seconds since changeMs: they open the next segment
    };

    Key keyFor(Activity a, const Sample& s) const;
    Segment close(const OpenSegment& seg, uint32_t endMs, float endDistanceM) const;
    void startSession(const Sample& s, Events& ev);
    void updateDistance(const Sample& s, Activity a, uint32_t dtMs);
    float countTreadmillDistance(uint32_t counterM);
    void fillGapFromTreadmillClock(const Sample& s);
    void updateSegments(const Sample& s, Activity a, Events& ev);
    void maybeEndSession(const Sample& s, Activity a, Events& ev);
    void closeSession(Events& ev);

    Config cfg_;

    bool inSession_ = false;
    uint32_t sessionStartMs_ = 0;
    Activity lastActivity_ = Activity::NoData;
    std::optional<uint32_t> lastTickMs_;
    uint32_t lastWalkMs_ = 0;
    std::optional<uint32_t> stopReportMs_;  // when the treadmill last reported a stop

    uint32_t walkMs_ = 0;
    float distanceM_ = 0.0f;
    float steps_ = 0.0f;
    float elevationGainM_ = 0.0f;
    float maxSpeedKmh_ = 0.0f;
    float maxInclinePct_ = 0.0f;
    uint32_t segmentCount_ = 0;
    HeartRateStats walkHeartRate_;  // walking seconds of the session only

    OpenSegment current_;
    std::optional<PendingChange> pending_;

    std::optional<uint32_t> lastTreadmillDistanceM_;

    // Used to recover walking time lost while the Bluetooth link was down.
    std::optional<uint16_t> lastElapsedS_;
    uint32_t walkMsAtLastElapsed_ = 0;
    uint32_t uptimeAtLastElapsed_ = 0;
    bool gapSinceLastElapsed_ = false;
};

}  // namespace walk
