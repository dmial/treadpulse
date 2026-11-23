// Walk logs in the board's own flash (LittleFS), kept until they reach the cloud.
//
//   /walks/<walk>/samples.csv   one row per second during the walk
//   /walks/<walk>/segments.csv  one row per constant speed/incline stretch
//   /walks/<walk>/summary.csv   header + one row; written last, so a walk is
//                               finished (and ready to upload) once it exists
//   /walks/<walk>/strap.bin     everything the heart rate strap sent, from the
//                               minute before the walk until the strap comes
//                               off after it, kStrapTailMs at most
//                               (lib/strap/strap_file.h)
//   /history.csv                every kept walk, one line each (archive.h);
//                               stays on the board after the walk is uploaded
//
// <walk> is "0042_2026-11-10T0815Z": a running number kept in NVS (it survives
// uploads and reflashing) and the start time in UTC. Without a clock it is
// "0042_nodate_9f3a1c2e", with a random tail so it stays unique in the cloud,
// until the clock gets set in the same boot (dateWalks()).
// All times in the files are UTC.
#pragma once

#include <Arduino.h>
#include <FS.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "archive.h"
#include "walk_log.h"
#include "walk_tracker.h"

class Storage {
   public:
    // The files of a walk, in upload order: the summary goes last, so in the
    // cloud too a walk is complete once its summary is there.
    static constexpr const char* kWalkFiles[] = {"samples.csv", "segments.csv", "strap.bin",
                                                 "summary.csv"};

    // After the walk ends the strap's data goes on into strap.bin, to catch the
    // pulse coming down, until the strap is off and at most this long. Till then
    // the walk is not complete: not pending, not uploaded.
    static constexpr uint32_t kStrapTailMs = 2 * 60 * 1000;

    bool begin();
    bool ok() const { return ok_; }

    // Closes walks left open by a power cut: rebuilds each one's segments from
    // its per-second log and writes its summary (marked as recovered); one too
    // short to keep is deleted. Call at boot, before a new walk can start.
    // Returns the count recovered.
    int recoverUnfinishedWalks();

    void startWalk(uint32_t startUptimeMs);
    void appendSample(const walk::SampleRow& row);
    void appendSegment(const walk::Segment& seg);
    void finishWalk(const walk::Summary& sum);
    // Drops the walk being recorded, too short to keep (walk::kMinKeptWalkS):
    // deletes its files and gives its number back.
    void discardWalk();

    // strap.bin of the walk in progress, or of the one that just ended, is
    // open: appendStrap() writes to it.
    bool strapRecording() const { return !strapWalk_.isEmpty(); }
    // The walk has ended but its strap.bin is still recording.
    bool strapTail() const { return strapRecording() && strapTailEndMs_.has_value(); }
    // How much longer that may go on at most; 0 when it is not on.
    uint32_t strapTailLeftMs(uint32_t now) const;
    // One record encoded as in strap_file.h. Stops, with a note, when the
    // file reaches the room it got at the walk's start.
    void appendStrap(const uint8_t* record, size_t len);
    // Closes a finished walk's strap.bin once the strap is off (`strapOff`: no
    // pulse for a while) or kStrapTailMs is up; the walk is complete from then
    // on. Call every tick.
    void tickStrap(uint32_t now, bool strapOff);

    // A walk begun before the clock was set gets its times once the clock is
    // set: the time in every line of its log, its segments and summary written
    // again with theirs, and its folder renamed after its start. The uptime
    // dates it only until the power goes, so a walk of an earlier boot stays
    // undated. Call once the clock is set, before uploading. Returns the count
    // dated.
    int dateWalks();
    // A walk of this boot still without its times: held back from upload, so
    // it reaches the cloud dated.
    bool waitsForClock(const String& walk) const;

    // Finished walks waiting for upload, oldest first.
    std::vector<String> pendingWalks();
    int pendingCount() const { return pendingCount_; }
    static String walkFile(const String& walk, const char* file);
    // Deletes a walk from flash, once it is safely in the cloud.
    void removeWalk(const String& walk);

    // Today's walks (day_log.h CSV) in /day.csv, apart from the walks, so the
    // daily total outlives their upload. "" when there is none yet.
    String readDayLog();
    // A failed write only costs the daily total, so it is logged, not fatal.
    void writeDayLog(const String& csv);

    // The history of every kept walk (archive.h CSV) in /history.csv. Making
    // room for a new walk deletes only unsent walk folders, never this file.
    void appendHistory(const archive::Entry& e);
    String readHistory();
    // Calls `line` for each line of the history, without reading it all at once.
    void forEachHistoryLine(const std::function<void(const std::string&)>& line);
    // Replaces the whole file, via a temporary one (e.g. after a merge).
    void writeHistory(const String& csv);

    // The last kept walk as the screen shows it (walk/last_walk.h) in
    // /last_walk.bin, so a restart brings it back. Replaced whole; empty when
    // there is none.
    void writeLastWalk(const std::vector<uint8_t>& bytes);
    std::vector<uint8_t> readLastWalk();

   private:
    // A walk of this boot begun before the clock was set, as recorded live,
    // to be written again with its times (dateWalks()).
    struct UndatedWalk {
        String name;
        int id = 0;
        uint32_t startUptimeMs = 0;
        std::vector<walk::Segment> segments;
        std::optional<walk::Summary> summary;  // once the walk is finished
    };

    bool isFinished(const String& walk);
    bool recoverWalk(const String& walk);
    bool dateWalk(const UndatedWalk& w, int64_t offsetS);
    UndatedWalk* undatedWalk(const String& walk);
    void forgetUndated(const String& walk);
    void makeRoom();
    bool writeSummary(const String& walk, const String& row);
    bool appendLine(const String& path, const String& header, const String& line);
    String readFile(const char* path);
    bool replaceFile(const char* path, const char* tmpPath, const String& content);
    void fail(const char* what);

    void openStrap(uint32_t now);
    bool writeStrap(const uint8_t* data, size_t len);
    void strapNote(uint32_t uptimeMs, const char* text);
    // Writes `why` as the last note, closes the file and lets its walk upload.
    void closeStrap(uint32_t now, const char* why);
    // Forgets the open strap.bin, with no last note (its walk is going away,
    // or the file cannot be written).
    void dropStrap();

    bool ok_ = false;
    int pendingCount_ = 0;
    int walkId_ = 0;
    String walk_;  // the walk being recorded, "" between walks
    File samples_;
    uint32_t lastFlushMs_ = 0;
    std::vector<UndatedWalk> undated_;

    File strap_;
    String strapWalk_;  // the walk whose strap.bin is open, "" when none is
    std::optional<uint32_t> strapTailEndMs_;  // once its walk has ended
    size_t strapRoom_ = 0;   // bytes it may take, from the free space at walk start
    size_t strapBytes_ = 0;  // written so far
    bool strapFull_ = false;
    uint32_t lastStrapFlushMs_ = 0;
};
