// Strap records on their way from the Bluetooth task to the walk's file: a
// ring of fixed-size slots in memory the caller provides (PSRAM on the board).
// When it is full, the oldest record makes room, so between walks it always
// holds the last moments before the next one starts. Not thread-safe: the
// caller locks around every call. Pure C++, host-testable.
#pragma once

#include <cstddef>
#include <cstdint>

#include "strap_file.h"

namespace strap {

// Plain data, so a block of raw memory serves as slots; push() fills every field.
struct Slot {
    uint32_t uptimeMs;
    uint16_t length;
    RecordType type;
    uint8_t payload[kMaxPayload];
};

class RecordQueue {
   public:
    RecordQueue() = default;
    // `slots` must outlive the queue; capacity 0 keeps nothing.
    RecordQueue(Slot* slots, size_t capacity) : slots_(slots), capacity_(capacity) {}

    // Adds a record. Full: the oldest one goes and counts as lost. A payload
    // over kMaxPayload is not kept and counts as lost too.
    void push(RecordType type, uint32_t uptimeMs, const uint8_t* payload, size_t length);

    bool empty() const { return count_ == 0; }
    size_t size() const { return count_; }
    // The oldest record. Only when not empty().
    const Slot& front() const { return slots_[head_]; }
    void pop();

    // Records lost since the last call.
    uint32_t takeLost();

   private:
    Slot* slots_ = nullptr;
    size_t capacity_ = 0;
    size_t head_ = 0;  // the oldest record
    size_t count_ = 0;
    uint32_t lost_ = 0;
};

}  // namespace strap
