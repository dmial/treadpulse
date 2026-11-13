#include "board_log.h"

#include <LittleFS.h>
#include <esp_heap_caps.h>

#include <cstdarg>
#include <deque>
#include <mutex>
#include <string>

#include "clock_sync.h"

namespace boardlog {

namespace {

constexpr const char* kFile = "/log.txt";
constexpr const char* kOldFile = "/log.old";
// Lines waiting for flush(); past this the oldest go (flash broken or not
// mounted yet for a long time).
constexpr size_t kMaxWaiting = 200;

std::mutex mutex;
std::deque<std::string> waiting;
size_t dropped = 0;
bool started = false;

// "2026-11-13T12:14:35Z" once the clock is set, "+00:01:05" since boot before.
std::string stamp() {
    if (clocksync::isSynced()) return clocksync::formatUtc(time(nullptr)).c_str();
    const uint32_t s = millis() / 1000;
    char buf[16];
    snprintf(buf, sizeof(buf), "+%02lu:%02lu:%02lu", static_cast<unsigned long>(s / 3600),
             static_cast<unsigned long>(s / 60 % 60), static_cast<unsigned long>(s % 60));
    return buf;
}

// Prints the text as given and queues each non-empty line of it for flash.
void add(const std::string& text) {
    Serial.print(text.c_str());
    const std::string when = stamp();
    std::lock_guard<std::mutex> lock(mutex);
    size_t from = 0;
    while (from < text.size()) {
        size_t end = text.find('\n', from);
        if (end == std::string::npos) end = text.size();
        if (end > from) {
            waiting.push_back(when + " " + text.substr(from, end - from) + "\n");
            if (waiting.size() > kMaxWaiting) {
                waiting.pop_front();
                ++dropped;
            }
        }
        from = end + 1;
    }
}

size_t fileSize(const char* path) {
    if (!LittleFS.exists(path)) return 0;  // open() would log an error
    File f = LittleFS.open(path, FILE_READ);
    if (!f) return 0;
    const size_t size = f.size();
    f.close();
    return size;
}

}  // namespace

void printf(const char* fmt, ...) {
    char small[160];
    va_list args;
    va_start(args, fmt);
    va_list again;
    va_copy(again, args);
    const int n = vsnprintf(small, sizeof(small), fmt, args);
    va_end(args);
    std::string text;
    if (n >= static_cast<int>(sizeof(small))) {
        text.resize(n);
        vsnprintf(&text[0], n + 1, fmt, again);
    } else if (n > 0) {
        text.assign(small, n);
    }
    va_end(again);
    if (!text.empty()) add(text);
}

void println(const char* line) { add(std::string(line) + "\n"); }

void begin() { started = true; }

void flush() {
    if (!started) return;
    std::deque<std::string> lines;
    size_t lost = 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (waiting.empty()) return;
        lines.swap(waiting);
        lost = dropped;
        dropped = 0;
    }

    File f = LittleFS.open(kFile, FILE_APPEND);
    if (!f) return;  // the lines still went to the serial port
    if (lost > 0) f.printf("%s [log] %u lines lost\n", stamp().c_str(), static_cast<unsigned>(lost));
    for (const std::string& line : lines) f.write(reinterpret_cast<const uint8_t*>(line.data()), line.size());
    const size_t size = f.size();
    f.close();

    if (size >= kMaxFileBytes) {
        if (LittleFS.exists(kOldFile)) LittleFS.remove(kOldFile);
        LittleFS.rename(kFile, kOldFile);
    }
}

Buffer readAll(size_t& size) {
    size = 0;
    const char* const files[] = {kOldFile, kFile};
    size_t total = 0;
    for (const char* path : files) total += fileSize(path);
    if (total == 0) return Buffer();

    auto* data = static_cast<uint8_t*>(heap_caps_malloc(total, MALLOC_CAP_SPIRAM));
    if (!data) data = static_cast<uint8_t*>(malloc(total));
    if (!data) return Buffer();
    Buffer buffer(data);

    for (const char* path : files) {
        if (!LittleFS.exists(path)) continue;
        File f = LittleFS.open(path, FILE_READ);
        if (!f) continue;
        size += f.read(data + size, total - size);
        f.close();
    }
    return buffer;
}

}  // namespace boardlog
