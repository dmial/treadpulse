// Parsers for the Bluetooth Fitness Machine Service (FTMS) "Treadmill Data"
// (UUID 0x2ACD) and "Fitness Machine Status" (UUID 0x2ADA) characteristics.
// Pure C++, no Arduino dependencies, so it can be unit-tested on the host.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace ftms {

// One decoded Treadmill Data notification. A treadmill may split its data over
// several notifications with different flag sets, so every field is optional.
struct TreadmillData {
    uint16_t flags = 0;

    std::optional<float> speedKmh;          // instantaneous speed
    std::optional<float> averageSpeedKmh;
    std::optional<uint32_t> totalDistanceM;  // cumulative since the treadmill workout started
    std::optional<float> inclinePct;
    std::optional<float> rampAngleDeg;
    std::optional<float> positiveElevationM;
    std::optional<float> negativeElevationM;
    std::optional<float> instantPaceKmMin;
    std::optional<float> averagePaceKmMin;
    std::optional<uint16_t> totalEnergyKcal;
    std::optional<uint16_t> energyPerHourKcal;
    std::optional<uint8_t> energyPerMinuteKcal;
    std::optional<uint8_t> heartRateBpm;
    std::optional<float> metabolicEquivalent;
    std::optional<uint16_t> elapsedTimeS;
    std::optional<uint16_t> remainingTimeS;
    std::optional<int16_t> forceOnBeltN;
    std::optional<int16_t> powerOutputW;
};

// Decodes a raw notification payload. Returns std::nullopt when the payload is
// shorter than its flags announce. Trailing bytes behind the known fields
// (vendor extensions, reserved flag bits) are ignored.
std::optional<TreadmillData> parseTreadmillData(const uint8_t* data, size_t len);

// Last known value of every metric the display and logger care about.
// Notifications only carry a subset of fields, so new values are merged in.
struct TreadmillState {
    float speedKmh = 0.0f;
    float inclinePct = 0.0f;
    std::optional<uint32_t> totalDistanceM;
    std::optional<uint16_t> elapsedTimeS;
    std::optional<uint16_t> totalEnergyKcal;

    void merge(const TreadmillData& d);
};

// Drops a glitch the Merach W50 sends about once a walk, around minute 12-15:
// one notification with speed, distance and workout time all zero while the
// belt keeps moving, then the old values again. A real stop looks different:
// the speed drops to 0 while distance and time still hold their values, and
// only seconds later do those go to 0 as well.
class ZeroGlitchFilter {
   public:
    // A glitch is suspected only this soon after the last notification taken
    // while the belt moved: zeros that keep coming longer, or that arrive
    // after a gap in the link, are a real stop.
    static constexpr uint32_t kHoldMs = 2000;

    // Whether notification `d`, received at `nowMs`, should be merged into
    // `state`. While a glitch is suspected, the speed-only notifications that
    // follow it are held back too.
    bool pass(const TreadmillState& state, const TreadmillData& d, uint32_t nowMs);

   private:
    std::optional<uint32_t> lastPassMs_;
    bool holding_ = false;
};

// What a Fitness Machine Status notification reports. Only the events the
// display acts on are told apart; everything else is Other.
enum class MachineEvent : uint8_t {
    Other,
    Stopped,     // stop pressed: the treadmill's workout is over
    Paused,      // pause pressed: the workout goes on after it
    SafetyStop,  // stopped by the safety key
    Started,     // started, or resumed after a pause
};

// Decodes a Fitness Machine Status payload; std::nullopt when it is empty.
std::optional<MachineEvent> parseMachineStatus(const uint8_t* data, size_t len);

}  // namespace ftms
