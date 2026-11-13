// Every kept walk, one line each, kept on the board for good (/history.csv):
// enough to show days and weeks of walking, with pulse zones, without the
// cloud. The per-second logs go to the cloud and leave the board; this stays.
// Pure C++, no Arduino dependencies, so it can be unit-tested on the host.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pulse_zones.h"
#include "walk_tracker.h"

namespace archive {

struct Entry {
    int64_t startEpoch = 0;  // UTC seconds
    int64_t endEpoch = 0;    // the last moment the belt was moving
    uint32_t walkSeconds = 0;  // belt moving
    uint32_t steps = 0;
    uint32_t distanceM = 0;
    uint32_t elevationGainM = 0;
    uint32_t pulseSeconds = 0;  // walking seconds with a pulse from the strap
    // The same seconds by heart rate zone, by the zone borders of that day:
    // [0] below zone 1 (rest), [1..5] the zones. Kept as counted then: zones
    // move as fitness changes, and a walk counts by the zones it was walked in.
    std::array<uint32_t, pulse::kZoneCount + 1> zoneSeconds{};
    uint16_t averageBpm = 0;  // 0: no pulse
    uint16_t maxBpm = 0;
    // The daily goal when the walk was kept, so a day stays on goal or off it
    // whatever the goal becomes later. 0: not known (a line from before the
    // history kept it; fillGoals() fixes those).
    uint32_t goalSeconds = 0;
};

extern const char* const kHeader;

// `goalSeconds`: the daily goal now, kept with the walk.
Entry fromSummary(const walk::Summary& s, int64_t startEpoch, int64_t endEpoch, uint32_t goalSeconds);

// One CSV line without the line break. Times as UTC "YYYY-MM-DDTHH:MM:SSZ".
std::string formatEntry(const Entry& e);
// Reads a line written by formatEntry. Columns added after the known ones are
// ignored, so an older firmware still reads a newer file; a line from before
// the goal column reads with goalSeconds 0. Returns nullopt for the header and
// for a damaged line (e.g. half-written when the power went out).
std::optional<Entry> parseEntry(const std::string& line);

// Every readable line of a whole file, in file order.
std::vector<Entry> parseAll(const std::string& csv);
// A whole file: header and one line per entry.
std::string formatAll(const std::vector<Entry>& entries);

// Adds the entries of `extra` that `entries` lacks and sorts them all by start.
// Two walks starting less than a minute apart are the same walk: a kept walk
// has at least a minute of walking, so two different ones never do.
// Returns how many entries were added.
int merge(std::vector<Entry>& entries, const std::vector<Entry>& extra);

// Gives the entries without a goal `goalSeconds`: for the lines kept before the
// history had the goal column, the goal of that time. Returns how many.
int fillGoals(std::vector<Entry>& entries, uint32_t goalSeconds);

}  // namespace archive
