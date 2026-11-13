#include "storage.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <esp_random.h>

#include <algorithm>

#include "board_log.h"
#include "clock_sync.h"
#include "walk_settings.h"

namespace {

constexpr const char* kWalksDir = "/walks";
constexpr const char* kSamples = "samples.csv";
constexpr const char* kSegments = "segments.csv";
constexpr const char* kSummary = "summary.csv";
constexpr const char* kDayLog = "/day.csv";
constexpr const char* kDayLogTmp = "/day.tmp";
constexpr const char* kHistory = "/history.csv";
constexpr const char* kHistoryTmp = "/history.tmp";

// Rows reach flash in batches: a power cut loses at most this much of the log.
constexpr uint32_t kFlushIntervalMs = 10000;
// Below this much free space, the oldest unsent walks make room for a new one
// (an hour of walking takes ~250 KB).
constexpr size_t kMinFreeBytes = 1024 * 1024;

String timeAt(uint32_t uptimeMs) {
    return clocksync::formatUtc(clocksync::epochAtUptime(uptimeMs));
}

String fromStd(const std::string& s) { return String(s.c_str()); }

String walkDir(const String& walk) { return String(kWalksDir) + "/" + walk; }

// Names of the entries in a directory (files or subdirectories).
std::vector<String> listDir(const String& path, bool wantDirs) {
    std::vector<String> names;
    File dir = LittleFS.open(path);
    if (!dir || !dir.isDirectory()) return names;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (f.isDirectory() == wantDirs) names.push_back(f.name());
        f.close();
    }
    dir.close();
    return names;
}

// Next walk number. Kept in NVS so it keeps counting after uploaded walks are
// deleted from flash.
int takeWalkId() {
    Preferences prefs;
    prefs.begin("walks", false);
    const uint32_t id = prefs.getUInt("next", 1);
    prefs.putUInt("next", id + 1);
    prefs.end();
    return static_cast<int>(id);
}

// Hands a dropped walk's number back, so the numbers have no gaps. Only the
// newest number goes back; a dropped walk never reached the cloud.
void giveBackWalkId(int id) {
    Preferences prefs;
    prefs.begin("walks", false);
    if (prefs.getUInt("next", 1) == static_cast<uint32_t>(id) + 1) prefs.putUInt("next", id);
    prefs.end();
}

}  // namespace

bool Storage::begin() {
    // Formats the partition on first use (or if it is damaged beyond repair).
    ok_ = LittleFS.begin(true);
    if (!ok_) {
        boardlog::println("[fs] cannot mount flash storage");
        return false;
    }
    if (!LittleFS.exists(kWalksDir)) LittleFS.mkdir(kWalksDir);
    pendingCount_ = static_cast<int>(pendingWalks().size());
    boardlog::printf("[fs] %u of %u KB used, %d walk(s) to upload\n",
                     static_cast<unsigned>(LittleFS.usedBytes() / 1024),
                     static_cast<unsigned>(LittleFS.totalBytes() / 1024), pendingCount_);
    return true;
}

String Storage::walkFile(const String& walk, const char* file) {
    return walkDir(walk) + "/" + file;
}

bool Storage::isFinished(const String& walk) { return LittleFS.exists(walkFile(walk, kSummary)); }

std::vector<String> Storage::pendingWalks() {
    std::vector<String> walks;
    if (!ok_) return walks;
    for (const String& w : listDir(kWalksDir, true)) {
        if (w != walk_ && isFinished(w)) walks.push_back(w);
    }
    // Names start with the zero-padded walk number, so this is oldest first.
    std::sort(walks.begin(), walks.end());
    return walks;
}

void Storage::removeWalk(const String& walk) {
    const String dir = walkDir(walk);
    for (const String& f : listDir(dir, false)) LittleFS.remove(dir + "/" + f);
    LittleFS.rmdir(dir);
    pendingCount_ = static_cast<int>(pendingWalks().size());
}

int Storage::recoverUnfinishedWalks() {
    if (!ok_) return 0;
    int recovered = 0;
    for (const String& w : listDir(kWalksDir, true)) {
        if (isFinished(w)) continue;
        if (recoverWalk(w)) ++recovered;
    }
    pendingCount_ = static_cast<int>(pendingWalks().size());
    return recovered;
}

bool Storage::recoverWalk(const String& walk) {
    File f = LittleFS.open(walkFile(walk, kSamples), FILE_READ);
    if (!f) {
        boardlog::printf("[fs] %s: no log, deleted\n", walk.c_str());
        removeWalk(walk);
        return false;
    }

    // A line the power cut left half-written has no line break: leave it out.
    bool lastLineComplete = true;
    const size_t size = f.size();
    if (size > 0) {
        f.seek(size - 1);
        lastLineComplete = f.read() == '\n';
        f.seek(0);
    }

    walk::WalkReplay replay(walkSettings());
    while (f.available()) {
        const String line = f.readStringUntil('\n');
        if (!f.available() && !lastLineComplete) break;
        if (auto row = walk::parseSampleRow(line.c_str())) replay.add(*row);
    }
    f.close();

    const walk::ReplayResult result = replay.finish();
    if (!result.summary) {
        // The power went out in the first seconds: nothing worth keeping.
        boardlog::printf("[fs] %s: no walking in the log, deleted\n", walk.c_str());
        removeWalk(walk);
        return false;
    }
    if (!result.summary->worthKeeping()) {
        boardlog::printf("[fs] %s: under a minute of walking, deleted\n", walk.c_str());
        removeWalk(walk);
        giveBackWalkId(atoi(walk.c_str()));  // "0042_..." -> 42
        return false;
    }

    // Rewrite all segments from the replay: the last one was never written live.
    // If the power goes out again here, the next boot simply redoes it, because
    // the summary is only written at the end.
    File seg = LittleFS.open(walkFile(walk, kSegments), FILE_WRITE);
    if (!seg) {
        fail("cannot rewrite segments file");
        return false;
    }
    seg.println(walk::kSegmentsHeader);
    for (const walk::Segment& s : result.segments) {
        seg.println(walk::formatSegmentRow(s, replay.timeAt(s.startMs), replay.timeAt(s.endMs))
                        .c_str());
    }
    seg.close();

    const walk::Summary& sum = *result.summary;
    const int id = atoi(walk.c_str());  // "0042_..." -> 42
    if (!writeSummary(walk, fromStd(walk::formatSummaryRow(id, sum, replay.timeAt(sum.startMs),
                                                           replay.timeAt(sum.endMs), true)))) {
        return false;
    }
    const std::optional<int64_t> start = replay.epochAt(sum.startMs);
    const std::optional<int64_t> end = replay.epochAt(sum.endMs);
    if (start && end) {
        appendHistory(archive::fromSummary(sum, *start, *end, userconfig::kDailyGoalMinutes * 60));
    } else {
        boardlog::printf("[fs] walk %s has no date: not in the history\n", walk.c_str());
    }
    boardlog::printf("[fs] walk %s recovered: %s, %.0f m\n", walk.c_str(),
                     walk::formatHms(sum.walkSeconds()).c_str(), sum.distanceM);
    return true;
}

void Storage::makeRoom() {
    while (LittleFS.totalBytes() - LittleFS.usedBytes() < kMinFreeBytes) {
        const std::vector<String> walks = pendingWalks();
        if (walks.empty()) return;
        boardlog::printf("[fs] memory full, deleting unsent walk %s\n", walks.front().c_str());
        removeWalk(walks.front());
    }
}

void Storage::startWalk(uint32_t startUptimeMs) {
    if (!ok_) return;
    makeRoom();
    walkId_ = takeWalkId();

    String date = clocksync::formatUtc(clocksync::epochAtUptime(startUptimeMs), "%Y-%m-%dT%H%MZ");
    if (date.isEmpty()) {
        char tail[24];
        snprintf(tail, sizeof(tail), "nodate_%08lx", static_cast<unsigned long>(esp_random()));
        date = tail;
    }
    char name[48];
    snprintf(name, sizeof(name), "%04d_%s", walkId_, date.c_str());
    walk_ = name;

    if (!LittleFS.mkdir(walkDir(walk_))) return fail("cannot create walk folder");
    samples_ = LittleFS.open(walkFile(walk_, kSamples), FILE_WRITE);
    if (!samples_) return fail("cannot create samples file");
    samples_.println(walk::kSamplesHeader);
    samples_.flush();
    lastFlushMs_ = startUptimeMs;

    if (!appendLine(walkFile(walk_, kSegments), walk::kSegmentsHeader, "")) return;
    boardlog::printf("[fs] walk %s started\n", walk_.c_str());
}

void Storage::appendSample(const walk::SampleRow& r) {
    if (!ok_ || !samples_) return;
    samples_.println(walk::formatSampleRow(r, timeAt(r.uptimeMs).c_str()).c_str());
    if (r.uptimeMs - lastFlushMs_ >= kFlushIntervalMs) {
        samples_.flush();  // survive a power cut mid-walk
        lastFlushMs_ = r.uptimeMs;
    }
}

void Storage::appendSegment(const walk::Segment& s) {
    if (!ok_ || walk_.isEmpty()) return;
    appendLine(walkFile(walk_, kSegments), walk::kSegmentsHeader,
               fromStd(walk::formatSegmentRow(s, timeAt(s.startMs).c_str(), timeAt(s.endMs).c_str())));
}

void Storage::finishWalk(const walk::Summary& sum) {
    if (!ok_ || walk_.isEmpty()) return;
    if (samples_) samples_.close();

    const String walk = walk_;
    walk_ = "";
    if (!writeSummary(walk, fromStd(walk::formatSummaryRow(walkId_, sum, timeAt(sum.startMs).c_str(),
                                                           timeAt(sum.endMs).c_str(), false)))) {
        return;
    }
    pendingCount_ = static_cast<int>(pendingWalks().size());
    boardlog::printf("[fs] walk %s finished: %s, %.0f m\n", walk.c_str(),
                     walk::formatHms(sum.walkSeconds()).c_str(), sum.distanceM);
}

void Storage::discardWalk() {
    if (!ok_ || walk_.isEmpty()) return;
    if (samples_) samples_.close();

    const String walk = walk_;
    walk_ = "";
    removeWalk(walk);
    giveBackWalkId(walkId_);
    boardlog::printf("[fs] walk %s: under a minute of walking, not kept\n", walk.c_str());
}

bool Storage::writeSummary(const String& walk, const String& row) {
    // Written to a temporary file and renamed: the summary marks the walk as
    // finished, so it must never exist half-written.
    const String tmp = walkFile(walk, "summary.tmp");
    File f = LittleFS.open(tmp, FILE_WRITE);
    if (!f) {
        fail("cannot write summary");
        return false;
    }
    f.println(walk::kSummaryHeader);
    f.println(row);
    f.close();
    if (!LittleFS.rename(tmp, walkFile(walk, kSummary))) {
        fail("cannot rename summary");
        return false;
    }
    return true;
}

bool Storage::appendLine(const String& path, const String& header, const String& line) {
    const bool isNew = !LittleFS.exists(path);
    File f = LittleFS.open(path, FILE_APPEND);
    if (!f) {
        fail("cannot open file for append");
        return false;
    }
    if (isNew) f.println(header);
    if (!line.isEmpty()) f.println(line);
    f.close();
    return true;
}

String Storage::readFile(const char* path) {
    if (!ok_ || !LittleFS.exists(path)) return "";
    File f = LittleFS.open(path, FILE_READ);
    if (!f) return "";
    String content = f.readString();
    f.close();
    return content;
}

// Via a temporary file, so a power cut never leaves half a file.
bool Storage::replaceFile(const char* path, const char* tmpPath, const String& content) {
    if (!ok_) return false;
    File f = LittleFS.open(tmpPath, FILE_WRITE);
    if (!f) return false;
    const bool written = f.print(content) == content.length();
    f.close();
    if (!written) {
        LittleFS.remove(tmpPath);
        return false;
    }
    if (LittleFS.rename(tmpPath, path)) return true;
    LittleFS.remove(path);
    return LittleFS.rename(tmpPath, path);
}

String Storage::readDayLog() { return readFile(kDayLog); }

void Storage::writeDayLog(const String& csv) {
    if (ok_ && !replaceFile(kDayLog, kDayLogTmp, csv)) boardlog::println("[fs] cannot save the day log");
}

void Storage::appendHistory(const archive::Entry& e) {
    if (!ok_) return;
    // A failed write costs one line of history, not the walk logs: no fail().
    const bool isNew = !LittleFS.exists(kHistory);
    File f = LittleFS.open(kHistory, FILE_APPEND);
    if (!f) {
        boardlog::println("[fs] cannot add the walk to the history");
        return;
    }
    if (isNew) f.println(archive::kHeader);
    f.println(archive::formatEntry(e).c_str());
    f.close();
}

String Storage::readHistory() { return readFile(kHistory); }

void Storage::forEachHistoryLine(const std::function<void(const std::string&)>& line) {
    if (!ok_ || !LittleFS.exists(kHistory)) return;
    File f = LittleFS.open(kHistory, FILE_READ);
    if (!f) return;
    while (f.available()) line(f.readStringUntil('\n').c_str());
    f.close();
}

void Storage::writeHistory(const String& csv) {
    if (ok_ && !replaceFile(kHistory, kHistoryTmp, csv)) boardlog::println("[fs] cannot save the history");
}

void Storage::fail(const char* what) {
    boardlog::printf("[fs] error: %s\n", what);
    ok_ = false;
}
