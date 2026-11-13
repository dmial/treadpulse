// Sends finished walks to the cloud receiver (cloud/ in this repo), oldest
// first, and deletes each walk from flash once all its files are accepted.
// Also sends the board's own log (board_log.h). Address and key come from
// src/secrets.h.
#pragma once

#include <Arduino.h>

#include "storage.h"

namespace uploader {

struct Result {
    int sent = 0;        // walks uploaded and deleted from flash
    bool failed = false; // stopped on an error; the rest stays queued for a retry
};

// Wi-Fi must be connected. Stops starting new walks after budgetMs.
Result sendPending(Storage& storage, uint32_t budgetMs);

// Sends the whole log, replacing the copy in the cloud. Wi-Fi must be
// connected. Returns false on an error.
bool sendLog();

}  // namespace uploader
