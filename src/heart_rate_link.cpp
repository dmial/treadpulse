#include "heart_rate_link.h"

void HeartRateLink::begin() {
    link_.begin([this](const uint8_t* data, size_t len) { onNotify(data, len); });
}

void HeartRateLink::onNotify(const uint8_t* data, size_t len) {
    auto m = heartrate::parseMeasurement(data, len);
    // Off-skin readings are dropped, so the pulse goes stale and shows as "no data".
    if (!m || !m->valid()) return;
    const uint32_t now = millis();

    portENTER_CRITICAL(&mux_);
    shared_.bpm = m->bpm;
    shared_.hasValid = true;
    shared_.lastValidMs = now;
    portEXIT_CRITICAL(&mux_);
}

HeartRateLink::Snapshot HeartRateLink::snapshot() {
    portENTER_CRITICAL(&mux_);
    Snapshot copy = shared_;
    portEXIT_CRITICAL(&mux_);
    copy.connected = link_.connected();
    return copy;
}
