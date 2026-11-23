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

// Little-endian: seconds per point, the pending readings (count, pulse sum,
// pulse count, last point), the number of points, then the points.
namespace {

constexpr size_t kPointBytes = 6;
constexpr size_t kHeaderBytes = 4 * 4 + kPointBytes + 4;

void putU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

void putU32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

uint16_t getU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }

uint32_t getU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
           static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

}  // namespace

void History::appendBytes(std::vector<uint8_t>& out) const {
    auto putPoint = [&](const Point& p) {
        putU16(out, p.bpm);
        putU16(out, p.speedCenti);
        putU16(out, static_cast<uint16_t>(p.inclineDeci));
    };
    out.reserve(out.size() + kHeaderBytes + points_.size() * kPointBytes);
    putU32(out, pointSeconds_);
    putU32(out, pending_.count);
    putU32(out, pending_.bpmSum);
    putU32(out, pending_.bpmCount);
    putPoint(pending_.last);
    putU32(out, static_cast<uint32_t>(points_.size()));
    for (const Point& p : points_) putPoint(p);
}

bool History::readBytes(const uint8_t* data, size_t len) {
    clear();
    if (!data || len < kHeaderBytes) return false;
    auto getPoint = [](const uint8_t* p) {
        Point pt;
        pt.bpm = getU16(p);
        pt.speedCenti = getU16(p + 2);
        pt.inclineDeci = static_cast<int16_t>(getU16(p + 4));
        return pt;
    };
    const uint32_t pointSeconds = getU32(data);
    const uint32_t count = getU32(data + kHeaderBytes - 4);
    if (pointSeconds == 0 || count > maxPoints_ || len != kHeaderBytes + count * kPointBytes) return false;

    pending_.count = getU32(data + 4);
    pending_.bpmSum = getU32(data + 8);
    pending_.bpmCount = getU32(data + 12);
    pending_.last = getPoint(data + 16);
    pointSeconds_ = pointSeconds;
    points_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) points_.push_back(getPoint(data + kHeaderBytes + i * kPointBytes));
    return true;
}

}  // namespace walk
