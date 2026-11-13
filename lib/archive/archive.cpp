#include "archive.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "walk_log.h"

namespace archive {

const char* const kHeader =
    "start,end,walk_time_s,steps,distance_m,elevation_gain_m,heart_rate_s,rest_s,zone1_s,"
    "zone2_s,zone3_s,zone4_s,zone5_s,avg_heart_rate_bpm,max_heart_rate_bpm,daily_goal_s";

namespace {

constexpr size_t kFieldCount = 15;  // without the goal, which came later
constexpr int64_t kSameWalkS = 60;

std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> fields;
    size_t from = 0;
    while (true) {
        const size_t comma = line.find(',', from);
        fields.push_back(line.substr(from, comma == std::string::npos ? std::string::npos
                                                                      : comma - from));
        if (comma == std::string::npos) break;
        from = comma + 1;
    }
    return fields;
}

// Whole number, digits only; `emptyOk` reads "" as 0.
std::optional<uint32_t> parseCount(const std::string& s, bool emptyOk = false) {
    if (s.empty()) return emptyOk ? std::optional<uint32_t>(0) : std::nullopt;
    if (s.size() > 9) return std::nullopt;
    uint32_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return std::nullopt;
        v = v * 10 + static_cast<uint32_t>(c - '0');
    }
    return v;
}

uint32_t rounded(float v) { return v > 0.0f ? static_cast<uint32_t>(std::lround(v)) : 0; }

}  // namespace

Entry fromSummary(const walk::Summary& s, int64_t startEpoch, int64_t endEpoch, uint32_t goalSeconds) {
    Entry e;
    e.goalSeconds = goalSeconds;
    e.startEpoch = startEpoch;
    e.endEpoch = endEpoch;
    e.walkSeconds = s.walkSeconds();
    e.steps = s.steps;
    e.distanceM = rounded(s.distanceM);
    e.elevationGainM = rounded(s.elevationGainM);
    // The replay of a cut-short log times walking and pulse a little apart.
    e.pulseSeconds = std::min(s.pulseSeconds, e.walkSeconds);
    e.zoneSeconds = s.zoneSeconds;
    if (s.averageHeartRateBpm) e.averageBpm = static_cast<uint16_t>(rounded(*s.averageHeartRateBpm));
    if (s.maxHeartRateBpm) e.maxBpm = *s.maxHeartRateBpm;
    return e;
}

std::string formatEntry(const Entry& e) {
    std::string out = walk::formatUtcTime(e.startEpoch) + "," + walk::formatUtcTime(e.endEpoch);
    auto add = [&out](uint32_t v) { out += "," + std::to_string(v); };
    add(e.walkSeconds);
    add(e.steps);
    add(e.distanceM);
    add(e.elevationGainM);
    add(e.pulseSeconds);
    for (uint32_t z : e.zoneSeconds) add(z);
    // Empty without a pulse, as in the walk's own summary.csv.
    if (e.averageBpm > 0) {
        add(e.averageBpm);
        add(e.maxBpm);
    } else {
        out += ",,";
    }
    out += ",";
    if (e.goalSeconds > 0) out += std::to_string(e.goalSeconds);
    return out;
}

std::optional<Entry> parseEntry(const std::string& raw) {
    std::string line = raw;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::vector<std::string> f = splitFields(line);
    if (f.size() < kFieldCount) return std::nullopt;

    const std::optional<int64_t> start = walk::parseUtcTime(f[0]);
    const std::optional<int64_t> end = walk::parseUtcTime(f[1]);
    if (!start || !end || *end < *start) return std::nullopt;

    Entry e;
    e.startEpoch = *start;
    e.endEpoch = *end;
    uint32_t* const counts[] = {&e.walkSeconds, &e.steps, &e.distanceM, &e.elevationGainM,
                                &e.pulseSeconds};
    size_t i = 2;
    for (uint32_t* c : counts) {
        const std::optional<uint32_t> v = parseCount(f[i++]);
        if (!v) return std::nullopt;
        *c = *v;
    }
    for (uint32_t& z : e.zoneSeconds) {
        const std::optional<uint32_t> v = parseCount(f[i++]);
        if (!v) return std::nullopt;
        z = *v;
    }
    const std::optional<uint32_t> avg = parseCount(f[i++], true);
    const std::optional<uint32_t> max = parseCount(f[i++], true);
    if (!avg || !max || *avg > 0xFFFF || *max > 0xFFFF) return std::nullopt;
    e.averageBpm = static_cast<uint16_t>(*avg);
    e.maxBpm = static_cast<uint16_t>(*max);
    if (f.size() > kFieldCount) {
        const std::optional<uint32_t> goal = parseCount(f[i++], true);
        if (!goal) return std::nullopt;
        e.goalSeconds = *goal;
    }
    return e;
}

std::vector<Entry> parseAll(const std::string& csv) {
    std::vector<Entry> entries;
    std::istringstream in(csv);
    std::string line;
    while (std::getline(in, line)) {
        if (auto e = parseEntry(line)) entries.push_back(*e);
    }
    return entries;
}

std::string formatAll(const std::vector<Entry>& entries) {
    std::string out = std::string(kHeader) + "\n";
    for (const Entry& e : entries) out += formatEntry(e) + "\n";
    return out;
}

int merge(std::vector<Entry>& entries, const std::vector<Entry>& extra) {
    int added = 0;
    for (const Entry& x : extra) {
        const bool known = std::any_of(entries.begin(), entries.end(), [&x](const Entry& e) {
            return std::llabs(e.startEpoch - x.startEpoch) < kSameWalkS;
        });
        if (known) continue;
        entries.push_back(x);
        ++added;
    }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.startEpoch < b.startEpoch;
    });
    return added;
}

int fillGoals(std::vector<Entry>& entries, uint32_t goalSeconds) {
    int filled = 0;
    for (Entry& e : entries) {
        if (e.goalSeconds > 0) continue;
        e.goalSeconds = goalSeconds;
        ++filled;
    }
    return filled;
}

}  // namespace archive
