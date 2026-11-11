// The per-second walk log (/walks/<walk>/samples.csv): one CSV row format
// shared by the live logger and by recovery after a power cut. Pure C++,
// host-testable.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "walk_tracker.h"

namespace walk {

extern const char* const kSamplesHeader;

// What the main loop logs once per second during a walk.
struct SampleRow {
    uint32_t uptimeMs = 0;
    Activity activity = Activity::NoData;
    uint32_t walkSeconds = 0;
    float speedKmh = 0.0f;
    float inclinePct = 0.0f;
    float distanceM = 0.0f;
    std::optional<uint32_t> treadmillDistanceM;
    std::optional<uint16_t> treadmillElapsedS;
    std::optional<uint16_t> heartRateBpm;
};

// A row read back from the log. The log keeps whole seconds of uptime.
struct LoggedRow {
    std::string time;  // UTC "YYYY-MM-DDTHH:MM:SSZ", "" when the clock was not set
    SampleRow row;
};

// One CSV line without the line break. `time` is "" when the clock is not set.
std::string formatSampleRow(const SampleRow& r, const std::string& time);

// Reads a line written by formatSampleRow. Returns nullopt for the header and
// for a damaged line (e.g. half-written when the power went out).
std::optional<LoggedRow> parseSampleRow(const std::string& line);

struct ReplayResult {
    std::vector<Segment> segments;
    std::optional<Summary> summary;  // empty when the log holds no walking at all
};

// Rebuilds the segments and summary of a walk whose log was cut short, by
// feeding its rows through the same WalkTracker that produced them live.
class WalkReplay {
   public:
    // Rows of one log, in file order.
    void add(const LoggedRow& logged);

    // Closes the walk at its last logged moment.
    ReplayResult finish();

    // UTC time of a logged moment, "" when the clock was not set.
    std::string timeAt(uint32_t uptimeMs) const;

   private:
    // UTC time minus uptime, in seconds, valid from `uptimeS` onwards.
    struct ClockRun {
        uint32_t uptimeS;
        int64_t offsetS;
    };

    void noteClock(const LoggedRow& logged);
    void collect(const Events& ev);

    WalkTracker tracker_;
    std::vector<Segment> segments_;
    std::optional<Summary> summary_;
    std::optional<SampleRow> last_;
    std::vector<ClockRun> clock_;
};

}  // namespace walk
