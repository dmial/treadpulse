#include "walk_history.h"

#include <algorithm>
#include <cmath>

namespace walk {

void History::clear() {
    points_.clear();
    pointSeconds_ = 1;
    pending_ = Pending();
}

void History::push(uint16_t bpm, float speedKmh, float inclinePct) {
    Point p;
    p.bpm = bpm;
    p.speedCenti = static_cast<uint16_t>(std::lround(std::max(0.0f, speedKmh) * 100.0f));
    p.inclineDeci = static_cast<int16_t>(std::lround(inclinePct * 10.0f));

    pending_.count++;
    if (bpm > 0) {
        pending_.bpmSum += bpm;
        pending_.bpmCount++;
    }
    pending_.last = p;
    if (pending_.count < pointSeconds_) return;
    if (points_.size() >= maxPoints_) {
        // Points now last twice as long: what is pending is half a point.
        halve();
        if (pending_.count < pointSeconds_) return;
    }

    Point whole = pending_.last;
    whole.bpm = pending_.bpmCount ? static_cast<uint16_t>((pending_.bpmSum + pending_.bpmCount / 2) /
                                                          pending_.bpmCount)
                                  : 0;
    pending_ = Pending();
    points_.push_back(whole);
}

void History::trimTo(uint32_t seconds) {
    if (seconds >= this->seconds()) return;
    pending_ = Pending();
    const size_t keep = (seconds + pointSeconds_ - 1) / pointSeconds_;
    if (keep < points_.size()) points_.resize(keep);
}

void History::halve() {
    std::vector<Point> merged;
    merged.reserve(points_.size() / 2 + 1);
    for (size_t i = 0; i + 1 < points_.size(); i += 2) {
        const Point& a = points_[i];
        const Point& b = points_[i + 1];
        Point m = b;  // speed and incline at the end of the pair
        if (a.bpm && b.bpm) {
            m.bpm = static_cast<uint16_t>((a.bpm + b.bpm + 1) / 2);
        } else {
            m.bpm = a.bpm ? a.bpm : b.bpm;
        }
        merged.push_back(m);
    }
    if (points_.size() % 2) merged.push_back(points_.back());
    points_.swap(merged);
    pointSeconds_ *= 2;
}

uint32_t History::seconds() const {
    return static_cast<uint32_t>(points_.size()) * pointSeconds_ + pending_.count;
}

uint32_t History::spanSeconds(uint32_t minSpanS) const { return std::max(seconds(), minSpanS); }

std::vector<Column> History::columns(int count, uint32_t minSpanS) const {
    std::vector<Column> cols(count > 0 ? count : 0);
    const double span = spanSeconds(minSpanS);
    if (cols.empty() || span <= 0 || points_.empty()) return cols;

    const double perPoint = pointSeconds_;
    for (int i = 0; i < count; ++i) {
        const double t0 = span * i / count;
        const double t1 = span * (i + 1) / count;
        size_t p0 = static_cast<size_t>(std::floor(t0 / perPoint));
        size_t p1 = static_cast<size_t>(std::ceil(t1 / perPoint));
        if (p1 <= p0) p1 = p0 + 1;
        if (p0 >= points_.size()) break;
        p1 = std::min(p1, points_.size());

        Column& c = cols[i];
        c.hasData = true;
        uint32_t sum = 0, n = 0;
        for (size_t p = p0; p < p1; ++p) {
            if (points_[p].bpm) {
                sum += points_[p].bpm;
                ++n;
            }
        }
        c.bpm = n ? static_cast<float>(sum) / n : 0.0f;
        c.speedKmh = points_[p1 - 1].speedCenti / 100.0f;
        c.inclinePct = points_[p1 - 1].inclineDeci / 10.0f;
    }
    return cols;
}

}  // namespace walk
