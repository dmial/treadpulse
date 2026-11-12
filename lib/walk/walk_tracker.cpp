#include "walk_tracker.h"

#include <algorithm>
#include <cmath>

namespace walk {

void HeartRateStats::add(std::optional<uint16_t> bpm) {
    if (!bpm) return;
    sum += *bpm;
    ++count;
    max = std::max(max, *bpm);
}

void HeartRateStats::merge(const HeartRateStats& other) {
    sum += other.sum;
    count += other.count;
    max = std::max(max, other.max);
}

std::optional<float> HeartRateStats::average() const {
    if (count == 0) return std::nullopt;
    return static_cast<float>(sum) / count;
}

std::optional<uint16_t> HeartRateStats::maximum() const {
    if (count == 0) return std::nullopt;
    return max;
}

const char* activityName(Activity a) {
    switch (a) {
        case Activity::Walk:
            return "walk";
        case Activity::Pause:
            return "pause";
        case Activity::NoData:
            return "no_data";
    }
    return "unknown";
}

WalkTracker::Key WalkTracker::keyFor(Activity a, const Sample& s) const {
    Key k;
    k.activity = a;
    if (a == Activity::Walk) {
        k.speedDeci = static_cast<int32_t>(std::lround(s.speedKmh * 10.0f));
        k.inclineDeci = static_cast<int32_t>(std::lround(s.inclinePct * 10.0f));
    }
    return k;
}

Segment WalkTracker::close(const OpenSegment& seg, uint32_t endMs, float endDistanceM) const {
    Segment out;
    out.activity = seg.key.activity;
    out.startMs = seg.startMs;
    out.endMs = endMs;
    out.speedKmh = seg.key.speedDeci / 10.0f;
    out.inclinePct = seg.key.inclineDeci / 10.0f;
    out.distanceM = std::max(0.0f, endDistanceM - seg.startDistanceM);
    out.averageHeartRateBpm = seg.heartRate.average();
    out.maxHeartRateBpm = seg.heartRate.maximum();
    return out;
}

Events WalkTracker::tick(const Sample& s) {
    Events ev;

    uint32_t dtMs = 0;
    if (lastTickMs_) dtMs = std::min(s.uptimeMs - *lastTickMs_, cfg_.maxTickGapMs);
    lastTickMs_ = s.uptimeMs;

    const Activity a = !s.fresh                                 ? Activity::NoData
                       : (s.speedKmh > cfg_.movingThresholdKmh) ? Activity::Walk
                                                                : Activity::Pause;
    lastActivity_ = a;

    if (!inSession_) {
        // Keep the treadmill counters as a baseline so the first session delta is right.
        if (s.fresh && s.treadmillDistanceM) countTreadmillDistance(*s.treadmillDistanceM);
        if (a == Activity::Walk) startSession(s, ev);
        return ev;
    }

    if (s.treadmillStopped) stopReportMs_ = s.uptimeMs;
    if (a == Activity::Walk) {
        walkMs_ += dtMs;
        if (s.heartRateBpm) {
            pulseMs_ += dtMs;
            if (cfg_.zones) zoneMs_[pulse::zoneOf(*cfg_.zones, *s.heartRateBpm)] += dtMs;
        }
        steps_ += cfg_.steps.over(s.speedKmh, dtMs);
        lastWalkMs_ = s.uptimeMs;
        maxSpeedKmh_ = std::max(maxSpeedKmh_, s.speedKmh);
        maxInclinePct_ = std::max(maxInclinePct_, s.inclinePct);
    }
    if (a == Activity::NoData) gapSinceLastElapsed_ = true;
    if (a == Activity::Walk) walkHeartRate_.add(s.heartRateBpm);

    // Segment boundaries use the distance reached before this step.
    updateSegments(s, a, ev);
    updateDistance(s, a, dtMs);
    fillGapFromTreadmillClock(s);
    maybeEndSession(s, a, ev);
    return ev;
}

void WalkTracker::startSession(const Sample& s, Events& ev) {
    inSession_ = true;
    sessionStartMs_ = s.uptimeMs;
    ev.sessionStarted = true;

    walkMs_ = 0;
    distanceM_ = 0.0f;
    steps_ = 0.0f;
    elevationGainM_ = 0.0f;
    maxSpeedKmh_ = s.speedKmh;
    maxInclinePct_ = s.inclinePct;
    segmentCount_ = 0;
    lastWalkMs_ = s.uptimeMs;
    stopReportMs_.reset();
    walkHeartRate_ = HeartRateStats{};
    walkHeartRate_.add(s.heartRateBpm);
    pulseMs_ = 0;
    zoneMs_ = {};

    current_ = OpenSegment{keyFor(Activity::Walk, s), s.uptimeMs, 0.0f};
    current_.heartRate.add(s.heartRateBpm);
    pending_.reset();

    lastElapsedS_ = s.treadmillElapsedS;
    walkMsAtLastElapsed_ = 0;
    uptimeAtLastElapsed_ = s.uptimeMs;
    gapSinceLastElapsed_ = false;
}

void WalkTracker::updateDistance(const Sample& s, Activity a, uint32_t dtMs) {
    if (!s.fresh) return;

    float delta = 0.0f;
    if (s.treadmillDistanceM) {
        // Prefer the treadmill's own counter: it also covers time we were disconnected.
        delta = countTreadmillDistance(*s.treadmillDistanceM);
    } else if (a == Activity::Walk) {
        delta = s.speedKmh / 3.6f * (dtMs / 1000.0f);
    }

    distanceM_ += delta;
    if (s.inclinePct > 0.0f) elevationGainM_ += delta * s.inclinePct / 100.0f;
}

// Meters the treadmill's counter moved since its last reading. A 0 is not a
// reading: the counter shows 0 during a pause and comes back where it was. A
// smaller value means a new workout after a stop, counted from 0.
float WalkTracker::countTreadmillDistance(uint32_t counterM) {
    if (counterM == 0) {
        if (!lastTreadmillDistanceM_) lastTreadmillDistanceM_ = 0u;  // a workout about to start
        return 0.0f;
    }
    float delta = 0.0f;
    if (lastTreadmillDistanceM_) {
        const uint32_t last = *lastTreadmillDistanceM_;
        delta = static_cast<float>(counterM >= last ? counterM - last : counterM);
    }
    lastTreadmillDistanceM_ = counterM;
    return delta;
}

void WalkTracker::fillGapFromTreadmillClock(const Sample& s) {
    // As with distance, a 0 on the treadmill's clock is a pause or a stop, not a reading.
    if (!s.fresh || !s.treadmillElapsedS || *s.treadmillElapsedS == 0) return;
    const uint16_t e = *s.treadmillElapsedS;
    if (lastElapsedS_ && e == *lastElapsedS_) return;  // no new clock value yet

    if (lastElapsedS_ && e > *lastElapsedS_ && gapSinceLastElapsed_) {
        // The link was down for a while: trust the treadmill's clock for that stretch,
        // but never add more than the wall-clock time that actually passed.
        const uint32_t treadmillMs = (e - *lastElapsedS_) * 1000u;
        const uint32_t wallMs = s.uptimeMs - uptimeAtLastElapsed_ + 1000u;
        const uint32_t ourMs = walkMs_ - walkMsAtLastElapsed_;
        const uint32_t credibleMs = std::min(treadmillMs, wallMs);
        if (credibleMs > ourMs + 2000u) {
            walkMs_ += credibleMs - ourMs;
            // The speed during the gap is unknown: the one after it stands in.
            steps_ += cfg_.steps.over(s.speedKmh, credibleMs - ourMs);
        }
    }

    lastElapsedS_ = e;
    walkMsAtLastElapsed_ = walkMs_;
    uptimeAtLastElapsed_ = s.uptimeMs;
    gapSinceLastElapsed_ = false;
}

void WalkTracker::updateSegments(const Sample& s, Activity a, Events& ev) {
    const Key k = keyFor(a, s);

    if (k == current_.key) {
        // A short blip came back to the current values: its seconds stay in this segment.
        if (pending_) current_.heartRate.merge(pending_->heartRate);
        pending_.reset();
        current_.heartRate.add(s.heartRateBpm);
        return;
    }

    if (!pending_) {
        pending_ = PendingChange{k, s.uptimeMs, distanceM_, s.uptimeMs};
    } else if (pending_->key != k) {
        // Still changing (e.g. speed stepping up 0.1 per button press).
        pending_->key = k;
        pending_->sinceMs = s.uptimeMs;
    }
    pending_->heartRate.add(s.heartRateBpm);

    if (s.uptimeMs - pending_->sinceMs < cfg_.stableMs) return;

    ev.closedSegments.push_back(close(current_, pending_->changeMs, pending_->changeDistanceM));
    ++segmentCount_;
    current_ = OpenSegment{pending_->key, pending_->changeMs, pending_->changeDistanceM,
                           pending_->heartRate};
    pending_.reset();
}

Events WalkTracker::endSession() {
    Events ev;
    if (inSession_) closeSession(ev);
    return ev;
}

void WalkTracker::maybeEndSession(const Sample& s, Activity a, Events& ev) {
    if (stopReportMs_ && s.uptimeMs - *stopReportMs_ > cfg_.stopReportWindowMs) {
        stopReportMs_.reset();  // the belt kept moving: not a stop after all
    }
    if (a == Activity::Walk) return;
    // A stop on the treadmill ends its workout: no need to wait out the idle time.
    if (!stopReportMs_ && s.uptimeMs - lastWalkMs_ < cfg_.endAfterIdleMs) return;
    closeSession(ev);
}

void WalkTracker::closeSession(Events& ev) {
    uint32_t endMs;
    if (current_.key.activity != Activity::Walk) {
        // Drop the trailing pause / no-data stretch: the walk ended where it began.
        endMs = current_.startMs;
    } else {
        // Idle never became a committed segment; close the walk where it stopped.
        endMs = pending_ ? pending_->changeMs : lastWalkMs_;
        const float endDist = pending_ ? pending_->changeDistanceM : distanceM_;
        ev.closedSegments.push_back(close(current_, endMs, endDist));
        ++segmentCount_;
    }

    Summary sum;
    sum.startMs = sessionStartMs_;
    sum.endMs = endMs;
    sum.walkMs = walkMs_;
    sum.distanceM = distanceM_;
    sum.maxSpeedKmh = maxSpeedKmh_;
    sum.maxInclinePct = maxInclinePct_;
    sum.elevationGainM = elevationGainM_;
    sum.segmentCount = segmentCount_;
    sum.averageHeartRateBpm = walkHeartRate_.average();
    sum.maxHeartRateBpm = walkHeartRate_.maximum();
    sum.steps = steps();
    sum.pulseSeconds = roundedSeconds(pulseMs_);
    for (size_t z = 0; z < zoneMs_.size(); ++z) sum.zoneSeconds[z] = roundedSeconds(zoneMs_[z]);
    ev.sessionEnded = sum;

    inSession_ = false;
    pending_.reset();
    stopReportMs_.reset();
}

}  // namespace walk
