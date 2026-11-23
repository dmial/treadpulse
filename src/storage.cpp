#include "storage.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <esp_random.h>

#include <algorithm>

#include "board_log.h"
#include "clock_sync.h"
#include "strap_file.h"
#include "walk_settings.h"

namespace {

constexpr const char* kWalksDir = "/walks";
constexpr const char* kSamples = "samples.csv";
constexpr const char* kSegments = "segments.csv";
constexpr const char* kSummary = "summary.csv";
constexpr const char* kStrap = "strap.bin";
constexpr const char* kDayLog = "/day.csv";
constexpr const char* kDayLogTmp = "/day.tmp";
constexpr const char* kHistory = "/history.csv";
constexpr const char* kHistoryTmp = "/history.tmp";

// Rows reach flash in batches: a power cut loses at most this much of the log.
constexpr uint32_t kFlushIntervalMs = 10000;
// Below this much free space, the oldest unsent walks make room for a new one
// (an hour of walking takes ~250 KB of CSV, plus ~3 MB of strap.bin).
constexpr size_t kMinFreeBytes = 1024 * 1024;
// strap.bin stops short of this much free space, which stays for the CSV files
// of the walk (and the log): even a 4-hour walk's samples.csv fits.
constexpr size_t kStrapReserveBytes = kMinFreeBytes + 512 * 1024;

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

// Moves `from` over `to`, which may already exist.
bool moveOver(const String& from, const String& to) {
    if (LittleFS.rename(from, to)) return true;
    LittleFS.remove(to);
    return LittleFS.rename(from, to);
}

// Writes `lines` over `path` through a temporary file next to it, so a power
// cut leaves the old file or the new one, never half of one.
bool writeOver(const String& path, const std::vector<std::string>& lines) {
    const String tmp = path + ".tmp";
    File f = LittleFS.open(tmp, FILE_WRITE);
    if (!f) return false;
    bool written = true;
    for (const std::string& line : lines) {
        written = written && f.println(line.c_str()) == line.size() + 2;
    }
    f.close();
    if (written && moveOver(tmp, path)) return true;
    LittleFS.remove(tmp);
    return false;
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
        // A finished walk still recording its strap.bin waits for it.
        if (w != walk_ && w != strapWalk_ && isFinished(w)) walks.push_back(w);
    }
    // Names start with the zero-padded walk number, so this is oldest first.
    std::sort(walks.begin(), walks.end());
    return walks;
}

void Storage::removeWalk(const String& walk) {
    if (walk == strapWalk_) dropStrap();
    const String dir = walkDir(walk);
    for (const String& f : listDir(dir, false)) LittleFS.remove(dir + "/" + f);
    LittleFS.rmdir(dir);
    forgetUndated(walk);
    pendingCount_ = static_cast<int>(pendingWalks().size());
}

Storage::UndatedWalk* Storage::undatedWalk(const String& walk) {
    for (UndatedWalk& w : undated_) {
        if (w.name == walk) return &w;
    }
    return nullptr;
}

void Storage::forgetUndated(const String& walk) {
    undated_.erase(std::remove_if(undated_.begin(), undated_.end(),
                                  [&walk](const UndatedWalk& w) { return w.name == walk; }),
                   undated_.end());
}

bool Storage::waitsForClock(const String& walk) const {
    return std::any_of(undated_.begin(), undated_.end(),
                       [&walk](const UndatedWalk& w) { return w.name == walk; });
}

int Storage::dateWalks() {
    if (!ok_ || undated_.empty() || !clocksync::isSynced()) return 0;
    const int64_t offsetS = static_cast<int64_t>(clocksync::epochAtUptime(0));  // UTC minus uptime
    int dated = 0;
    std::vector<UndatedWalk> recording;
    for (UndatedWalk& w : undated_) {
        // Dated once it is finished, and its strap.bin too: dating renames the folder.
        if (!w.summary || w.name == strapWalk_) {
            recording.push_back(std::move(w));
            continue;
        }
        if (dateWalk(w, offsetS)) ++dated;
    }
    // One that could not be dated uploads as it is rather than wait for good.
    undated_.swap(recording);
    return dated;
}

bool Storage::dateWalk(const UndatedWalk& w, int64_t offsetS) {
    const auto timeAtUptime = [offsetS](uint32_t uptimeMs) {
        return walk::formatUtcTime(offsetS + uptimeMs / 1000);
    };

    // The per-second log line by line, through a temporary file: an hour of
    // walking is ~250 KB, too much to hold at once.
    const String samples = walkFile(w.name, kSamples);
    const String samplesTmp = samples + ".tmp";
    File in = LittleFS.open(samples, FILE_READ);
    File out = LittleFS.open(samplesTmp, FILE_WRITE);
    bool written = in && out;
    while (written && in.available()) {
        String line = in.readStringUntil('\n');
        if (line.endsWith("\r")) line.remove(line.length() - 1);
        const std::string dated = walk::datedSampleLine(line.c_str(), offsetS);
        written = out.println(dated.c_str()) == dated.size() + 2;
    }
    if (in) in.close();
    if (out) out.close();
    if (!written || !moveOver(samplesTmp, samples)) {
        LittleFS.remove(samplesTmp);
        boardlog::printf("[fs] walk %s: cannot date its log, uploads without dates\n", w.name.c_str());
        return false;
    }

    // The segments and summary as they would have been written with a clock.
    // A power cut here leaves the summary either undated or dated; should it
    // leave none, the next boot rebuilds it from the log, now dated.
    std::vector<std::string> segments{walk::kSegmentsHeader};
    for (const walk::Segment& s : w.segments) {
        segments.push_back(walk::formatSegmentRow(s, timeAtUptime(s.startMs), timeAtUptime(s.endMs)));
    }
    const walk::Summary& sum = *w.summary;
    const std::vector<std::string> summary{
        walk::kSummaryHeader,
        walk::formatSummaryRow(w.id, sum, timeAtUptime(sum.startMs), timeAtUptime(sum.endMs), false)};
    if (!writeOver(walkFile(w.name, kSegments), segments) ||
        !writeOver(walkFile(w.name, kSummary), summary)) {
        boardlog::printf("[fs] walk %s: cannot date its segments or summary\n", w.name.c_str());
        return false;
    }

    // Renamed last: a power cut before this leaves the walk under its undated
    // name, with the times it already has.
    char name[48];
    snprintf(name, sizeof(name), "%04d_%s", w.id,
             clocksync::formatUtc(static_cast<time_t>(offsetS + w.startUptimeMs / 1000),
                                  "%Y-%m-%dT%H%MZ")
                 .c_str());
    if (!LittleFS.rename(walkDir(w.name), walkDir(name))) {
        boardlog::printf("[fs] walk %s dated, but cannot be renamed to %s\n", w.name.c_str(), name);
        return true;
    }
    boardlog::printf("[fs] walk %s dated: %s\n", w.name.c_str(), name);
    return true;
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
    // The last walk's strap.bin ends here, so that walk can upload.
    closeStrap(millis(), "after-walk recording cut short: a new walk started");
    makeRoom();
    walkId_ = takeWalkId();

    String date = clocksync::formatUtc(clocksync::epochAtUptime(startUptimeMs), "%Y-%m-%dT%H%MZ");
    const bool hasDate = !date.isEmpty();
    if (!hasDate) {
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
    if (!hasDate) {
        UndatedWalk u;
        u.name = walk_;
        u.id = walkId_;
        u.startUptimeMs = startUptimeMs;
        undated_.push_back(std::move(u));
    }
    openStrap(startUptimeMs);
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
    if (UndatedWalk* u = undatedWalk(walk_)) u->segments.push_back(s);
}

void Storage::finishWalk(const walk::Summary& sum) {
    if (!ok_ || walk_.isEmpty()) return;
    if (samples_) samples_.close();

    const String walk = walk_;
    walk_ = "";
    // strap.bin goes on for a while: the pulse coming down after the walk.
    if (strapWalk_ == walk) {
        const uint32_t now = millis();
        strapNote(now, "walk ended");
        strapTailEndMs_ = now + kStrapTailMs;
    }
    if (!writeSummary(walk, fromStd(walk::formatSummaryRow(walkId_, sum, timeAt(sum.startMs).c_str(),
                                                           timeAt(sum.endMs).c_str(), false)))) {
        return;
    }
    if (UndatedWalk* u = undatedWalk(walk)) u->summary = sum;
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
    return moveOver(tmpPath, path);
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

void Storage::openStrap(uint32_t now) {
    const size_t used = LittleFS.usedBytes();
    const size_t total = LittleFS.totalBytes();
    const size_t free = total > used ? total - used : 0;
    strapRoom_ = free > kStrapReserveBytes ? free - kStrapReserveBytes : 0;
    strapBytes_ = 0;
    strapFull_ = false;
    strapTailEndMs_.reset();

    // A walk without strap.bin is still a whole walk: no fail().
    strap_ = LittleFS.open(walkFile(walk_, kStrap), FILE_WRITE);
    if (!strap_) {
        boardlog::printf("[fs] walk %s: cannot create strap.bin\n", walk_.c_str());
        return;
    }
    strapWalk_ = walk_;
    lastStrapFlushMs_ = now;
    if (!writeStrap(strap::kFileHeader, strap::kFileHeaderSize)) return;
    strapNote(now, "walk started");
}

bool Storage::writeStrap(const uint8_t* data, size_t len) {
    if (strap_.write(data, len) == len) {
        strapBytes_ += len;
        return true;
    }
    boardlog::printf("[fs] walk %s: cannot write strap.bin, it ends at %u KB\n", strapWalk_.c_str(),
                     static_cast<unsigned>(strapBytes_ / 1024));
    dropStrap();
    pendingCount_ = static_cast<int>(pendingWalks().size());
    return false;
}

void Storage::strapNote(uint32_t uptimeMs, const char* text) {
    if (!strapRecording()) return;
    uint8_t record[strap::kMaxRecordSize];
    const size_t len = strap::encodeRecord(strap::RecordType::Note, uptimeMs,
                                           reinterpret_cast<const uint8_t*>(text), strlen(text), record);
    // Notes are few and tell the file's story, so each goes to flash at once:
    // a power cut never leaves the file empty, even with no strap connected.
    if (writeStrap(record, len)) strap_.flush();
}

void Storage::appendStrap(const uint8_t* record, size_t len) {
    if (!strapRecording()) return;
    if (strapBytes_ + len > strapRoom_) {
        if (!strapFull_) {
            strapFull_ = true;
            strapNote(millis(), "memory nearly full: the strap's data stops here");
            boardlog::printf("[fs] walk %s: memory nearly full, strap.bin stops at %u KB\n",
                             strapWalk_.c_str(), static_cast<unsigned>(strapBytes_ / 1024));
        }
        return;
    }
    if (!writeStrap(record, len)) return;
    const uint32_t now = millis();
    if (now - lastStrapFlushMs_ >= kFlushIntervalMs) {
        strap_.flush();  // survive a power cut
        lastStrapFlushMs_ = now;
    }
}

uint32_t Storage::strapTailLeftMs(uint32_t now) const {
    if (!strapTail()) return 0;
    const int32_t left = static_cast<int32_t>(*strapTailEndMs_ - now);
    return left > 0 ? static_cast<uint32_t>(left) : 0;
}

void Storage::tickStrap(uint32_t now, bool strapOff) {
    if (!strapTail()) return;
    if (strapOff) {
        closeStrap(now, "after-walk recording ended: strap off");
    } else if (static_cast<int32_t>(now - *strapTailEndMs_) >= 0) {
        closeStrap(now, "after-walk recording ended: time up");
    }
}

void Storage::closeStrap(uint32_t now, const char* why) {
    if (!strapRecording()) return;
    strapNote(now, why);
    if (!strapRecording()) return;  // the note could not be written
    boardlog::printf("[fs] walk %s: strap.bin %u KB\n", strapWalk_.c_str(),
                     static_cast<unsigned>(strapBytes_ / 1024));
    dropStrap();
    pendingCount_ = static_cast<int>(pendingWalks().size());
}

void Storage::dropStrap() {
    if (strap_) strap_.close();
    strapWalk_ = "";
    strapTailEndMs_.reset();
}

void Storage::fail(const char* what) {
    boardlog::printf("[fs] error: %s\n", what);
    ok_ = false;
}
