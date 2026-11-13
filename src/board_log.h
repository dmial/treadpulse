// The board's own log. Every line goes to the USB serial port, as before, and
// also into /log.txt in flash, so it outlives a reboot and reaches the cloud
// with the walks (uploader.h): a failure is still there to read later, even
// when no computer was listening at the time.
//
// Kept small: once /log.txt passes kMaxFileBytes it becomes /log.old
// (replacing the one before), so the log takes at most twice that.
//
// Lines may come from any task (Wi-Fi events, Bluetooth callbacks). They wait
// in memory until flush() writes them from the main loop, so only the main
// loop touches the flash. Each line in flash starts with the time: UTC once
// the clock is set, the time since boot ("+00:01:05") before.
#pragma once

#include <Arduino.h>

#include <cstdlib>
#include <memory>

namespace boardlog {

constexpr size_t kMaxFileBytes = 32 * 1024;

// Logs printf-style; one call may hold several lines, each ending with '\n'.
void printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
// Logs one line (the line break is added).
void println(const char* line);

// Starts keeping the log in flash; LittleFS must be mounted. Lines logged
// before go in with the next flush().
void begin();
// Writes the waiting lines to flash. Call often from the main loop.
void flush();

struct FreeDeleter {
    void operator()(uint8_t* p) const { free(p); }
};
using Buffer = std::unique_ptr<uint8_t, FreeDeleter>;

// The whole log in flash, oldest line first (flush() first for the newest
// lines). Empty (size 0) when there is none or no memory for it.
Buffer readAll(size_t& size);

}  // namespace boardlog
