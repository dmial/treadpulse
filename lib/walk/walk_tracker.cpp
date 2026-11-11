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
        if (s.fresh) {
            if (s.treadmillDistanceM) lastTreadmillDistanceM_ = s.treadmillDistanceM;
        }
        if (a == Activity::Walk) startSession(s, ev);
        return ev;
    }

    if (a == Activity::Walk) {
        walkMs_ += dtMs;
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
    elevationGainM_ = 0.0f;
    maxSpeedKmh_ = s.speedKmh;
    maxInclinePct_ = s.inclinePct;
    segmentCount_ = 0;
    lastWalkMs_ = s.uptimeMs;
    walkHeartRate_ = HeartRateStats{};
    walkHeartRate_.add(s.heartRateBpm);

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
        const uint32_t now = *s.treadmillDistanceM;
        if (lastTreadmillDistanceM_ && now >= *lastTreadmillDistanceM_) {
            delta = static_cast<float>(now - *lastTreadmillDistanceM_);
        }
        // A smaller value means the treadmill reset its counter; just re-baseline.
        lastTreadmillDistanceM_ = now;
    } else if (a == Activity::Walk) {
        delta = s.speedKmh / 3.6f * (dtMs / 1000.0f);
    }

    distanceM_ += delta;
    if (s.inclinePct > 0.0f) elevationGainM_ += delta * s.inclinePct / 100.0f;
}

void WalkTracker::fillGapFromTreadmillClock(const Sample& s) {
    if (!s.fresh || !s.treadmillElapsedS) return;
    const uint16_t e = *s.treadmillElapsedS;
    if (lastElapsedS_ && e == *lastElapsedS_) return;  // no new clock value yet

    if (lastElapsedS_ && e > *lastElapsedS_ && gapSinceLastElapsed_) {
        // The link was down for a while: trust the treadmill's clock for that stretch,
        // but never add more than the wall-clock time that actually passed.
        const uint32_t treadmillMs = (e - *lastElapsedS_) * 1000u;
        const uint32_t wallMs = s.uptimeMs - uptimeAtLastElapsed_ + 1000u;
        const uint32_t ourMs = walkMs_ - walkMsAtLastElapsed_;
        const uint32_t credibleMs = std::min(treadmillMs, wallMs);
        if (credibleMs > ourMs + 2000u) walkMs_ += credibleMs - ourMs;
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
    if (a == Activity::Walk) return;
    if (s.uptimeMs - lastWalkMs_ < cfg_.endAfterIdleMs) return;
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
    ev.sessionEnded = sum;

    inSession_ = false;
    pending_.reset();
}

}  // namespace walk
