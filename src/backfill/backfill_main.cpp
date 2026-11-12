// Computer-side tool for walk logs downloaded from the cloud, run with the same
// walk code as the board (lib/walk), the step formula and zone borders of
// src/user_config.h.
//
//   pio run -e backfill
//   .pio/build/backfill/program history <walks dir> src/history_seed.h
//   .pio/build/backfill/program rewrite <walks dir> <out dir>
//
// <walks dir> holds one folder per walk with its samples.csv (and summary.csv
// for rewrite), as the board uploads them: walks/<walk>/<file> in the cloud.
//
// history: history lines (archive.h) for walks from before the board kept a
// history. Flash the firmware afterwards: it adds the walks its history lacks.
//
// rewrite: each walk's three files as today's firmware would write them, into
// <out dir>/<walk>/: walking time, distance and steps in samples.csv come from
// the replay, segments and summary are rebuilt. For logs from a firmware that
// counted something wrong (distance before 2026-11-12) or lacked a column
// (steps). Walks with under a minute of walking, which the board no longer
// keeps, are left out and listed.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "archive.h"
#include "walk_log.h"
#include "walk_settings.h"

namespace fs = std::filesystem;

namespace {

std::vector<fs::path> walkDirs(const char* root) {
    std::vector<fs::path> dirs;
    for (const fs::directory_entry& d : fs::directory_iterator(root)) {
        if (d.is_directory() && fs::exists(d.path() / "samples.csv")) dirs.push_back(d.path());
    }
    std::sort(dirs.begin(), dirs.end());
    return dirs;
}

std::vector<walk::LoggedRow> readLog(const fs::path& samples) {
    std::vector<walk::LoggedRow> rows;
    std::ifstream in(samples);
    std::string line;
    while (std::getline(in, line)) {
        if (auto row = walk::parseSampleRow(line)) rows.push_back(*row);
    }
    return rows;
}

// Totals from the replay, not from the log: an old log may have them wrong.
constexpr auto kTotals = walk::WalkReplay::Totals::Replayed;

bool kept(const walk::ReplayResult& r) { return r.summary && r.summary->worthKeeping(); }

int history(const char* walksDir, const char* outPath) {
    std::vector<archive::Entry> entries;
    for (const fs::path& dir : walkDirs(walksDir)) {
        const std::string name = dir.filename().string();
        walk::WalkReplay replay(walkSettings());
        for (const walk::LoggedRow& row : readLog(dir / "samples.csv")) replay.add(row);
        const walk::ReplayResult result = replay.finish(kTotals);
        const std::optional<int64_t> start =
            kept(result) ? replay.epochAt(result.summary->startMs) : std::nullopt;
        const std::optional<int64_t> end =
            kept(result) ? replay.epochAt(result.summary->endMs) : std::nullopt;
        if (!start || !end) {
            printf("%s  skipped: no date or under a minute of walking\n", name.c_str());
            continue;
        }
        const archive::Entry e = archive::fromSummary(*result.summary, *start, *end);
        archive::merge(entries, {e});
        printf("%s  %s  %4u min  pulse %4u min\n", name.c_str(),
               walk::formatUtcTime(e.startEpoch).c_str(), e.walkSeconds / 60, e.pulseSeconds / 60);
    }

    std::ofstream out(outPath);
    out << "// Walks from before the board kept a history, made by env:backfill\n"
           "// (src/backfill/backfill_main.cpp). Personal data: stays out of git.\n"
           "#pragma once\n\n"
           "namespace historyseed {\n"
           "constexpr const char* kCsv = R\"csv(\n"
        << archive::formatAll(entries) << ")csv\";\n}  // namespace historyseed\n";
    if (!out) {
        fprintf(stderr, "cannot write %s\n", outPath);
        return 1;
    }
    printf("%u walks written to %s\n", static_cast<unsigned>(entries.size()), outPath);
    return 0;
}

// The board writes its files with Arduino's println: CRLF line ends.
void writeLines(const fs::path& path, const std::vector<std::string>& lines) {
    std::ofstream out(path, std::ios::binary);
    for (const std::string& l : lines) out << l << "\r\n";
    if (!out) {
        fprintf(stderr, "cannot write %s\n", path.string().c_str());
        exit(1);
    }
}

// The last field of the old summary row: "yes" for a walk closed after a power loss.
bool wasRecovered(const fs::path& summary) {
    std::ifstream in(summary);
    std::string line, last;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) last = line;
    }
    const size_t comma = last.rfind(',');
    return comma != std::string::npos && last.substr(comma + 1) == "yes";
}

int rewrite(const char* walksDir, const char* outDir) {
    int written = 0;
    for (const fs::path& dir : walkDirs(walksDir)) {
        const std::string name = dir.filename().string();
        walk::WalkReplay replay(walkSettings());
        std::vector<walk::LoggedRow> rows = readLog(dir / "samples.csv");
        std::vector<std::string> samples = {walk::kSamplesHeader};
        for (walk::LoggedRow& logged : rows) {
            replay.add(logged);
            const walk::WalkTracker& t = replay.tracker();
            logged.row.walkSeconds = t.walkSeconds();
            logged.row.distanceM = t.distanceM();
            logged.row.steps = t.steps();
            samples.push_back(walk::formatSampleRow(logged.row, logged.time));
        }
        const walk::ReplayResult result = replay.finish(kTotals);
        if (!kept(result)) {
            printf("%s  left out: under a minute of walking\n", name.c_str());
            continue;
        }

        std::vector<std::string> segments = {walk::kSegmentsHeader};
        for (const walk::Segment& s : result.segments) {
            segments.push_back(
                walk::formatSegmentRow(s, replay.timeAt(s.startMs), replay.timeAt(s.endMs)));
        }
        const walk::Summary& sum = *result.summary;
        const int id = atoi(name.c_str());  // "0042_..." -> 42
        const std::vector<std::string> summary = {
            walk::kSummaryHeader,
            walk::formatSummaryRow(id, sum, replay.timeAt(sum.startMs), replay.timeAt(sum.endMs),
                                   wasRecovered(dir / "summary.csv"))};

        const fs::path out = fs::path(outDir) / name;
        fs::create_directories(out);
        writeLines(out / "samples.csv", samples);
        writeLines(out / "segments.csv", segments);
        writeLines(out / "summary.csv", summary);
        printf("%s  %s  %5.0f m  %5u steps\n", name.c_str(),
               walk::formatHms(sum.walkSeconds()).c_str(), sum.distanceM,
               static_cast<unsigned>(sum.steps));
        ++written;
    }
    printf("%d walks written to %s\n", written, outDir);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string mode = argc == 4 ? argv[1] : "";
    if (mode == "history") return history(argv[2], argv[3]);
    if (mode == "rewrite") return rewrite(argv[2], argv[3]);
    fprintf(stderr,
            "usage: %s history <walks dir> <output .h>\n"
            "       %s rewrite <walks dir> <output dir>\n",
            argv[0], argv[0]);
    return 2;
}
