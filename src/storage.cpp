#include "storage.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <esp_random.h>

#include <algorithm>

#include "clock_sync.h"
#include "walk_settings.h"

namespace {

constexpr const char* kWalksDir = "/walks";
constexpr const char* kSamples = "samples.csv";
constexpr const char* kSegments = "segments.csv";
constexpr const char* kSummary = "summary.csv";
constexpr const char* kDayLog = "/day.csv";
constexpr const char* kDayLogTmp = "/day.tmp";

// Rows reach flash in batches: a power cut loses at most this much of the log.
constexpr uint32_t kFlushIntervalMs = 10000;
// Below this much free space, the oldest unsent walks make room for a new one
// (an hour of walking takes ~250 KB).
constexpr size_t kMinFreeBytes = 1024 * 1024;

const char* kSegmentsHeader =
    "start,end,duration_s,state,speed_kmh,incline_pct,distance_m,avg_heart_rate_bpm,"
    "max_heart_rate_bpm";
const char* kSummaryHeader =
    "id,start,end,walk_time,walk_time_s,distance_m,steps,avg_speed_kmh,max_speed_kmh,"
    "max_incline_pct,elevation_gain_m,avg_heart_rate_bpm,max_heart_rate_bpm,segments,"
    "recovered";

String timeAt(uint32_t uptimeMs) {
    return clocksync::formatUtc(clocksync::epochAtUptime(uptimeMs));
}

// Pulse summary as two CSV fields "avg,max"; both empty without a strap.
String heartRateFields(const std::optional<float>& avg, const std::optional<uint16_t>& max) {
    char buf[16] = ",";
    if (avg && max) {
        snprintf(buf, sizeof(buf), "%.0f,%u", *avg, static_cast<unsigned>(*max));
    }
    return String(buf);
}

String hms(uint32_t seconds) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu", static_cast<unsigned long>(seconds / 3600),
             static_cast<unsigned long>(seconds / 60 % 60), static_cast<unsigned long>(seconds % 60));
    return String(buf);
}

String segmentLine(const walk::Segment& s, const String& start, const String& end) {
    char buf[160];
    snprintf(buf, sizeof(buf), "%s,%s,%lu,%s,%.1f,%.1f,%.1f,%s", start.c_str(), end.c_str(),
             static_cast<unsigned long>(s.durationS()), walk::activityName(s.activity),
             s.speedKmh, s.inclinePct, s.distanceM,
             heartRateFields(s.averageHeartRateBpm, s.maxHeartRateBpm).c_str());
    return String(buf);
}

String summaryLine(int id, const walk::Summary& sum, const String& start, const String& end,
                   bool recovered) {
    const uint32_t walkS = sum.walkSeconds();
    char buf[256];
    snprintf(buf, sizeof(buf), "%d,%s,%s,%s,%lu,%.0f,%lu,%.2f,%.1f,%.1f,%.1f,%s,%lu,%s", id,
             start.c_str(), end.c_str(), hms(walkS).c_str(), static_cast<unsigned long>(walkS),
             sum.distanceM, static_cast<unsigned long>(sum.steps), sum.averageSpeedKmh(),
             sum.maxSpeedKmh, sum.maxInclinePct, sum.elevationGainM,
             heartRateFields(sum.averageHeartRateBpm, sum.maxHeartRateBpm).c_str(),
             static_cast<unsigned long>(sum.segmentCount), recovered ? "yes" : "");
    return String(buf);
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
        Serial.println("[fs] cannot mount flash storage");
        return false;
    }
    if (!LittleFS.exists(kWalksDir)) LittleFS.mkdir(kWalksDir);
    pendingCount_ = static_cast<int>(pendingWalks().size());
    Serial.printf("[fs] %u of %u KB used, %d walk(s) to upload\n",
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
        Serial.printf("[fs] %s: no log, deleted\n", walk.c_str());
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
        Serial.printf("[fs] %s: no walking in the log, deleted\n", walk.c_str());
        removeWalk(walk);
        return false;
    }
    if (!result.summary->worthKeeping()) {
        Serial.printf("[fs] %s: under a minute of walking, deleted\n", walk.c_str());
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
    seg.println(kSegmentsHeader);
    for (const walk::Segment& s : result.segments) {
        seg.println(segmentLine(s, fromStd(replay.timeAt(s.startMs)),
                                fromStd(replay.timeAt(s.endMs))));
    }
    seg.close();

    const walk::Summary& sum = *result.summary;
    const int id = atoi(walk.c_str());  // "0042_..." -> 42
    if (!writeSummary(walk, summaryLine(id, sum, fromStd(replay.timeAt(sum.startMs)),
                                        fromStd(replay.timeAt(sum.endMs)), true))) {
        return false;
    }
    Serial.printf("[fs] walk %s recovered: %s, %.0f m\n", walk.c_str(),
                  hms(sum.walkSeconds()).c_str(), sum.distanceM);
    return true;
}

void Storage::makeRoom() {
    while (LittleFS.totalBytes() - LittleFS.usedBytes() < kMinFreeBytes) {
        const std::vector<String> walks = pendingWalks();
        if (walks.empty()) return;
        Serial.printf("[fs] memory full, deleting unsent walk %s\n", walks.front().c_str());
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

    if (!appendLine(walkFile(walk_, kSegments), kSegmentsHeader, "")) return;
    Serial.printf("[fs] walk %s started\n", walk_.c_str());
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
    appendLine(walkFile(walk_, kSegments), kSegmentsHeader,
               segmentLine(s, timeAt(s.startMs), timeAt(s.endMs)));
}

void Storage::finishWalk(const walk::Summary& sum) {
    if (!ok_ || walk_.isEmpty()) return;
    if (samples_) samples_.close();

    const String walk = walk_;
    walk_ = "";
    if (!writeSummary(walk, summaryLine(walkId_, sum, timeAt(sum.startMs), timeAt(sum.endMs),
                                        false))) {
        return;
    }
    pendingCount_ = static_cast<int>(pendingWalks().size());
    Serial.printf("[fs] walk %s finished: %s, %.0f m\n", walk.c_str(),
                  hms(sum.walkSeconds()).c_str(), sum.distanceM);
}

void Storage::discardWalk() {
    if (!ok_ || walk_.isEmpty()) return;
    if (samples_) samples_.close();

    const String walk = walk_;
    walk_ = "";
    removeWalk(walk);
    giveBackWalkId(walkId_);
    Serial.printf("[fs] walk %s: under a minute of walking, not kept\n", walk.c_str());
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
    f.println(kSummaryHeader);
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

String Storage::readDayLog() {
    if (!ok_ || !LittleFS.exists(kDayLog)) return "";
    File f = LittleFS.open(kDayLog, FILE_READ);
    if (!f) return "";
    String csv = f.readString();
    f.close();
    return csv;
}

void Storage::writeDayLog(const String& csv) {
    if (!ok_) return;
    // Via a temporary file, so a power cut never leaves half a day log.
    File f = LittleFS.open(kDayLogTmp, FILE_WRITE);
    if (!f) {
        Serial.println("[fs] cannot write the day log");
        return;
    }
    f.print(csv);
    f.close();
    if (!LittleFS.rename(kDayLogTmp, kDayLog)) {
        LittleFS.remove(kDayLog);
        if (!LittleFS.rename(kDayLogTmp, kDayLog)) Serial.println("[fs] cannot save the day log");
    }
}

void Storage::fail(const char* what) {
    Serial.printf("[fs] error: %s\n", what);
    ok_ = false;
}
