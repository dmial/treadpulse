#include "pmd.h"

#include <initializer_list>

namespace pmd {

namespace {

constexpr uint8_t kReplyCode = 0xF0;
constexpr uint8_t kFeaturesCode = 0x0F;
constexpr uint8_t kErrorAlreadyInState = 0x06;

// Setting types in a start command; each one is followed by a count (1 here)
// and the value as uint16 little-endian.
constexpr uint8_t kSampleRate = 0x00;
constexpr uint8_t kResolution = 0x01;
constexpr uint8_t kRange = 0x02;

struct Setting {
    uint8_t type;
    uint16_t value;
};

std::vector<uint8_t> startCommand(Measurement m, std::initializer_list<Setting> settings) {
    std::vector<uint8_t> cmd{kOpStart, static_cast<uint8_t>(m)};
    for (const Setting& s : settings) {
        cmd.push_back(s.type);
        cmd.push_back(1);
        cmd.push_back(static_cast<uint8_t>(s.value));
        cmd.push_back(static_cast<uint8_t>(s.value >> 8));
    }
    return cmd;
}

}  // namespace

std::vector<uint8_t> startEcgCommand() {
    return startCommand(Measurement::Ecg,
                        {{kSampleRate, kEcgSampleRateHz}, {kResolution, kEcgResolutionBits}});
}

std::vector<uint8_t> startAccCommand() {
    return startCommand(Measurement::Acc, {{kSampleRate, kAccSampleRateHz},
                                           {kResolution, kAccResolutionBits},
                                           {kRange, kAccRangeG}});
}

std::optional<Reply> parseReply(const uint8_t* data, size_t len) {
    if (data == nullptr || len < 4 || data[0] != kReplyCode) return std::nullopt;
    Reply r;
    r.opCode = data[1];
    r.measurement = data[2];
    r.error = data[3];
    return r;
}

bool started(const Reply& r) { return r.error == 0 || r.error == kErrorAlreadyInState; }

const char* errorText(uint8_t error) {
    switch (error) {
        case 0x00:
            return "success";
        case 0x01:
            return "invalid op code";
        case 0x02:
            return "invalid measurement type";
        case 0x03:
            return "not supported";
        case 0x04:
            return "invalid length";
        case 0x05:
            return "invalid parameter";
        case 0x06:
            return "already running";
        case 0x07:
            return "invalid resolution";
        case 0x08:
            return "invalid sample rate";
        case 0x09:
            return "invalid range";
        case 0x0A:
            return "MTU too small";
        case 0x0B:
            return "invalid number of channels";
        case 0x0C:
            return "invalid state";
        case 0x0D:
            return "strap is charging";
        default:
            return "unknown error";
    }
}

std::optional<uint8_t> parseFeatures(const uint8_t* data, size_t len) {
    if (data == nullptr || len < 2 || data[0] != kFeaturesCode) return std::nullopt;
    return data[1];
}

std::string featureNames(uint8_t features) {
    static const char* const kNames[8] = {"ECG",       "optical pulse", "accelerometer", "pulse intervals",
                                          "type 4",    "gyroscope",     "magnetometer",  "type 7"};
    std::string names;
    for (int bit = 0; bit < 8; ++bit) {
        if (!(features & (1u << bit))) continue;
        if (!names.empty()) names += ", ";
        names += kNames[bit];
    }
    return names.empty() ? "none" : names;
}

std::optional<uint8_t> frameMeasurement(const uint8_t* data, size_t len) {
    if (data == nullptr || len < kFrameHeaderSize) return std::nullopt;
    return data[0];
}

}  // namespace pmd
