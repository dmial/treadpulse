#include "record_queue.h"

#include <cstring>

namespace strap {

void RecordQueue::push(RecordType type, uint32_t uptimeMs, const uint8_t* payload, size_t length) {
    if (capacity_ == 0 || length > kMaxPayload) {
        ++lost_;
        return;
    }
    if (count_ == capacity_) {
        pop();
        ++lost_;
    }
    Slot& s = slots_[(head_ + count_) % capacity_];
    s.type = type;
    s.uptimeMs = uptimeMs;
    s.length = static_cast<uint16_t>(length);
    if (length > 0) std::memcpy(s.payload, payload, length);
    ++count_;
}

void RecordQueue::pop() {
    if (count_ == 0) return;
    head_ = (head_ + 1) % capacity_;
    --count_;
}

uint32_t RecordQueue::takeLost() {
    const uint32_t lost = lost_;
    lost_ = 0;
    return lost;
}

}  // namespace strap
