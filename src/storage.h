// Walk logs in the board's own flash (LittleFS), kept until they reach the cloud.
//
//   /walks/<walk>/samples.csv   one row per second during the walk
//   /walks/<walk>/segments.csv  one row per constant speed/incline stretch
//   /walks/<walk>/summary.csv   header + one row; written last, so a walk is
//                               finished (and ready to upload) once it exists
//   /history.csv                every kept walk, one line each (archive.h);
//                               stays on the board after the walk is uploaded
//
// <walk> is "0042_2026-11-10T0815Z": a running number kept in NVS (it survives
// uploads and reflashing) and the start time in UTC. Without a clock it is
// "0042_nodate_9f3a1c2e", with a random tail so it stays unique in the cloud.
// All times in the files are UTC.
#pragma once

#include <Arduino.h>
#include <FS.h>

#include <vector>

#include "archive.h"
#include "walk_log.h"
#include "walk_tracker.h"

class Storage {
   public:
    // The files of a walk, in upload order: the summary goes last, so in the
    // cloud too a walk is complete once its summary is there.
    static constexpr const char* kWalkFiles[] = {"samples.csv", "segments.csv", "summary.csv"};

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
    // Replaces the whole file, via a temporary one (e.g. after a merge).
    void writeHistory(const String& csv);

   private:
    bool isFinished(const String& walk);
    bool recoverWalk(const String& walk);
    void makeRoom();
    bool writeSummary(const String& walk, const String& row);
    bool appendLine(const String& path, const String& header, const String& line);
    String readFile(const char* path);
    bool replaceFile(const char* path, const char* tmpPath, const String& content);
    void fail(const char* what);

    bool ok_ = false;
    int pendingCount_ = 0;
    int walkId_ = 0;
    String walk_;  // the walk being recorded, "" between walks
    File samples_;
    uint32_t lastFlushMs_ = 0;
};
