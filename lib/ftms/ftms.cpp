#include "ftms.h"

namespace ftms {

namespace {

// Flag bits of the Treadmill Data characteristic (FTMS v1.0, section 4.9.1.1).
// Note bit 0 is inverted: 0 means "instantaneous speed is present".
constexpr uint16_t kMoreData = 1u << 0;
constexpr uint16_t kAverageSpeed = 1u << 1;
constexpr uint16_t kTotalDistance = 1u << 2;
constexpr uint16_t kInclination = 1u << 3;
constexpr uint16_t kElevationGain = 1u << 4;
constexpr uint16_t kInstantPace = 1u << 5;
constexpr uint16_t kAveragePace = 1u << 6;
constexpr uint16_t kExpendedEnergy = 1u << 7;
constexpr uint16_t kHeartRate = 1u << 8;
constexpr uint16_t kMetabolicEquivalent = 1u << 9;
constexpr uint16_t kElapsedTime = 1u << 10;
constexpr uint16_t kRemainingTime = 1u << 11;
constexpr uint16_t kForceAndPower = 1u << 12;

// Little-endian cursor over the payload that fails softly on overrun.
class Reader {
   public:
    Reader(const uint8_t* data, size_t len) : data_(data), len_(len) {}

    bool ok() const { return ok_; }

    uint8_t u8() {
        if (!need(1)) return 0;
        return data_[pos_++];
    }

    uint16_t u16() {
        if (!need(2)) return 0;
        uint16_t v = static_cast<uint16_t>(data_[pos_] | (data_[pos_ + 1] << 8));
        pos_ += 2;
        return v;
    }

    int16_t s16() { return static_cast<int16_t>(u16()); }

    uint32_t u24() {
        if (!need(3)) return 0;
        uint32_t v = static_cast<uint32_t>(data_[pos_]) |
                     (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
                     (static_cast<uint32_t>(data_[pos_ + 2]) << 16);
        pos_ += 3;
        return v;
    }

   private:
    bool need(size_t n) {
        if (!ok_ || pos_ + n > len_) {
            ok_ = false;
            return false;
        }
        return true;
    }

    const uint8_t* data_;
    size_t len_;
    size_t pos_ = 0;
    bool ok_ = true;
};

}  // namespace

std::optional<TreadmillData> parseTreadmillData(const uint8_t* data, size_t len) {
    if (data == nullptr || len < 2) return std::nullopt;

    Reader r(data, len);
    TreadmillData d;
    d.flags = r.u16();
    const uint16_t f = d.flags;

    if (!(f & kMoreData)) d.speedKmh = r.u16() / 100.0f;
    if (f & kAverageSpeed) d.averageSpeedKmh = r.u16() / 100.0f;
    if (f & kTotalDistance) d.totalDistanceM = r.u24();
    if (f & kInclination) {
        d.inclinePct = r.s16() / 10.0f;
        d.rampAngleDeg = r.s16() / 10.0f;
    }
    if (f & kElevationGain) {
        d.positiveElevationM = r.u16() / 10.0f;
        d.negativeElevationM = r.u16() / 10.0f;
    }
    if (f & kInstantPace) d.instantPaceKmMin = r.u8() / 10.0f;
    if (f & kAveragePace) d.averagePaceKmMin = r.u8() / 10.0f;
    if (f & kExpendedEnergy) {
        d.totalEnergyKcal = r.u16();
        d.energyPerHourKcal = r.u16();
        d.energyPerMinuteKcal = r.u8();
    }
    if (f & kHeartRate) d.heartRateBpm = r.u8();
    if (f & kMetabolicEquivalent) d.metabolicEquivalent = r.u8() / 10.0f;
    if (f & kElapsedTime) d.elapsedTimeS = r.u16();
    if (f & kRemainingTime) d.remainingTimeS = r.u16();
    if (f & kForceAndPower) {
        d.forceOnBeltN = r.s16();
        d.powerOutputW = r.s16();
    }

    if (!r.ok()) return std::nullopt;
    return d;
}

void TreadmillState::merge(const TreadmillData& d) {
    if (d.speedKmh) speedKmh = *d.speedKmh;
    if (d.inclinePct) inclinePct = *d.inclinePct;
    if (d.totalDistanceM) totalDistanceM = d.totalDistanceM;
    if (d.elapsedTimeS) elapsedTimeS = d.elapsedTimeS;
    if (d.totalEnergyKcal) totalEnergyKcal = d.totalEnergyKcal;
}

bool ZeroGlitchFilter::pass(const TreadmillState& state, const TreadmillData& d,
                            uint32_t nowMs) {
    const bool moving =
        state.speedKmh > 0.0f && lastPassMs_ && nowMs - *lastPassMs_ < kHoldMs;
    const bool zeroSpeed = d.speedKmh && *d.speedKmh == 0.0f;
    const bool zeroTotals = d.totalDistanceM && *d.totalDistanceM == 0 && d.elapsedTimeS &&
                            *d.elapsedTimeS == 0;
    const bool noTotals = !d.totalDistanceM && !d.elapsedTimeS;
    if (moving && zeroSpeed && (zeroTotals || (holding_ && noTotals))) {
        holding_ = true;
        return false;
    }
    holding_ = false;
    lastPassMs_ = nowMs;
    return true;
}

std::optional<MachineEvent> parseMachineStatus(const uint8_t* data, size_t len) {
    if (data == nullptr || len < 1) return std::nullopt;
    switch (data[0]) {
        case 0x02:  // stopped or paused by the user; the parameter tells which
            if (len < 2) return MachineEvent::Other;
            if (data[1] == 0x01) return MachineEvent::Stopped;
            if (data[1] == 0x02) return MachineEvent::Paused;
            return MachineEvent::Other;
        case 0x03:
            return MachineEvent::SafetyStop;
        case 0x04:
            return MachineEvent::Started;
        default:
            return MachineEvent::Other;
    }
}

}  // namespace ftms
