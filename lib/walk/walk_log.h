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

// The other two files of a walk (segments.csv, summary.csv): headers and rows.
// Times are UTC "YYYY-MM-DDTHH:MM:SSZ", "" when the clock was not set.
extern const char* const kSegmentsHeader;
extern const char* const kSummaryHeader;
std::string formatSegmentRow(const Segment& s, const std::string& start, const std::string& end);
std::string formatSummaryRow(int id, const Summary& sum, const std::string& start,
                             const std::string& end, bool recovered);
// "1:02:03"
std::string formatHms(uint32_t seconds);

// UTC "YYYY-MM-DDTHH:MM:SSZ" <-> seconds since 1970-01-01T00:00:00Z.
std::optional<int64_t> parseUtcTime(const std::string& s);
std::string formatUtcTime(int64_t t);

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
    // Steps so far in the walk; empty in a log written before steps were counted.
    std::optional<uint32_t> steps;
};

// A row read back from the log. The log keeps whole seconds of uptime.
struct LoggedRow {
    std::string time;  // UTC "YYYY-MM-DDTHH:MM:SSZ", "" when the clock was not set
    SampleRow row;
};

// One CSV line without the line break. `time` is "" when the clock is not set.
std::string formatSampleRow(const SampleRow& r, const std::string& time);

// Reads a line written by formatSampleRow, or by a firmware before steps were
// counted (one column less). Returns nullopt for the header and for a damaged
// line (e.g. half-written when the power went out).
std::optional<LoggedRow> parseSampleRow(const std::string& line);

struct ReplayResult {
    std::vector<Segment> segments;
    std::optional<Summary> summary;  // empty when the log holds no walking at all
};

// Rebuilds the segments and summary of a walk whose log was cut short, by
// feeding its rows through the same WalkTracker that produced them live.
class WalkReplay {
   public:
    // The step formula only counts for a log without steps in it.
    explicit WalkReplay(Config cfg = Config()) : tracker_(cfg) {}

    // Rows of one log, in file order.
    void add(const LoggedRow& logged);

    // Which totals the summary takes: the live ones logged every second (they
    // saw the exact timing), or the replay's own, for a log written by a
    // firmware that counted them wrong (the distance, before 2026-11-12).
    enum class Totals { Logged, Replayed };

    // Closes the walk at its last logged moment.
    ReplayResult finish(Totals totals = Totals::Logged);

    // UTC time of a logged moment, "" when the clock was not set.
    std::string timeAt(uint32_t uptimeMs) const;
    // The same in seconds since 1970; empty when the clock was not set.
    std::optional<int64_t> epochAt(uint32_t uptimeMs) const;

    // The tracker after the rows added so far: what the live log would have
    // held in its walking time, distance and steps columns at that row.
    const WalkTracker& tracker() const { return tracker_; }

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
