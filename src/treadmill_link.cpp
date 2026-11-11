#include "treadmill_link.h"

void TreadmillLink::begin() {
    link_.begin([this](const uint8_t* data, size_t len) { onNotify(data, len); });
}

void TreadmillLink::onNotify(const uint8_t* data, size_t len) {
    auto parsed = ftms::parseTreadmillData(data, len);
    if (!parsed) return;
    const uint32_t now = millis();

    portENTER_CRITICAL(&mux_);
    shared_.state.merge(*parsed);
    shared_.hasData = true;
    shared_.lastNotifyMs = now;
    portEXIT_CRITICAL(&mux_);
}

TreadmillLink::Snapshot TreadmillLink::snapshot() {
    portENTER_CRITICAL(&mux_);
    Snapshot copy = shared_;
    portEXIT_CRITICAL(&mux_);
    copy.connected = link_.connected();
    return copy;
}
