#include "heart_rate_link.h"

#include <esp_heap_caps.h>

#include "board_log.h"

namespace {

// Records waiting for the walk's file, in PSRAM: about four arrive a second
// (pulse, ECG and accelerometer frames), so this holds over four minutes.
constexpr size_t kQueueSlots = 1024;
// The strap negotiates the MTU right after connecting; ECG needs pmd::kMinMtu.
constexpr uint32_t kMtuWaitMs = 1000;
// "Just works" pairing takes a second or two; past this the strap is set up anyway.
constexpr uint32_t kPairingWaitMs = 5000;
constexpr uint32_t kReplyTimeoutMs = 3000;
// A stream without a frame for this long is started again, at most this often.
constexpr uint32_t kStallMs = 15000;
constexpr uint32_t kRestartEveryMs = 60000;

const NimBLEUUID kBatteryService(static_cast<uint16_t>(0x180F));
const NimBLEUUID kBatteryLevel(static_cast<uint16_t>(0x2A19));

// Time since `then`; zero when `then` was set on another task a moment after
// the caller read millis().
uint32_t since(uint32_t now, uint32_t then) {
    const int32_t d = static_cast<int32_t>(now - then);
    return d > 0 ? static_cast<uint32_t>(d) : 0;
}

}  // namespace

void HeartRateLink::begin() {
    void* slots = heap_caps_malloc(sizeof(strap::Slot) * kQueueSlots, MALLOC_CAP_SPIRAM);
    if (slots) {
        queue_ = strap::RecordQueue(static_cast<strap::Slot*>(slots), kQueueSlots);
    } else {
        boardlog::println("[hr] no memory for the strap's records: walks get no strap.bin data");
    }
    link_.onConnect([this](NimBLEClient* client) { onConnected(client); });
    link_.begin([this](const uint8_t* data, size_t len) { onNotify(data, len); });
    // How the strap gets paired (onConnected): Secure Connections, no passkey
    // (it has no screen or buttons), and bonded, so later connects encrypt with
    // the stored key. Paired without a bond, an H10 refused its measurements and
    // dropped the link.
    NimBLEDevice::setSecurityAuth(true, false, true);
}

void HeartRateLink::onNotify(const uint8_t* data, size_t len) {
    // Every notification goes to the file, off-skin ones included.
    push(strap::RecordType::HeartRate, data, len);

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

void HeartRateLink::onPmdData(const uint8_t* data, size_t len) {
    push(strap::RecordType::PmdData, data, len);
    const auto measurement = pmd::frameMeasurement(data, len);
    if (!measurement) return;
    for (Stream& s : streams_) {
        if (static_cast<uint8_t>(s.type) != *measurement) continue;
        s.lastFrameMs.store(millis());
        s.gotFrame.store(true);
    }
}

void HeartRateLink::onPmdControl(const uint8_t* data, size_t len) {
    push(strap::RecordType::PmdControl, data, len);
    const auto reply = pmd::parseReply(data, len);
    if (!reply || reply->opCode != pmd::kOpStart) return;
    for (Stream& s : streams_) {
        if (static_cast<uint8_t>(s.type) != reply->measurement) continue;
        // This task is the only writer, so the read and the store need no lock.
        const uint32_t count = (s.startReplies.load() >> 8) + 1;
        s.startReplies.store(count << 8 | reply->error);
    }
}

void HeartRateLink::onConnected(NimBLEClient* client) {
    pmdControl_ = nullptr;
    pairing_ = nullptr;
    waiting_ = nullptr;
    wasConnected_ = true;

    const uint32_t start = millis();
    while (client->getMTU() < pmd::kMinMtu && millis() - start < kMtuWaitMs) delay(20);
    const NimBLEAttValue battery = client->getValue(kBatteryService, kBatteryLevel);
    char text[96];
    snprintf(text, sizeof(text), "connected %s, battery %d%%, MTU %u", link_.deviceName().c_str(),
             battery.size() > 0 ? battery.data()[0] : -1, static_cast<unsigned>(client->getMTU()));
    note(text);
    boardlog::printf("[hr] %s\n", text);

    if (!client->getService(pmd::kServiceUuid)) {
        boardlog::println("[hr] no Polar measurement service: pulse only");
        return;
    }
    const NimBLEConnInfo link = client->getConnInfo();
    boardlog::printf("[hr] link: %s, %s\n", link.isEncrypted() ? "encrypted" : "not encrypted",
                     link.isBonded() ? "bonded" : "not bonded");
    // An H10 took the start commands over a plain link and stayed silent, so
    // the link is encrypted first. loop() waits for that without standing
    // still, then sets the measurements up and starts the streams.
    if (client->getConnInfo().isEncrypted()) {
        setUpPmd(client);
    } else if (client->secureConnection(true)) {
        pairing_ = client;
        pairingSinceMs_ = millis();
    } else {
        boardlog::printf("[hr] cannot pair with the strap (error %d): trying without\n",
                         client->getLastError());
        setUpPmd(client);
    }
}

void HeartRateLink::setUpPmd(NimBLEClient* client) {
    NimBLERemoteService* service = client->getService(pmd::kServiceUuid);
    if (!service) {
        boardlog::println("[hr] no Polar measurement service: pulse only");
        return;
    }
    NimBLERemoteCharacteristic* control = service->getCharacteristic(pmd::kControlUuid);
    NimBLERemoteCharacteristic* data = service->getCharacteristic(pmd::kDataUuid);
    // Replies come as indications, frames as notifications.
    const char* failed =
        !control || !data ? "characteristics not found"
        : !control->subscribe(false,
                              [this](NimBLERemoteCharacteristic*, uint8_t* bytes, size_t len, bool) {
                                  onPmdControl(bytes, len);
                              })
            ? "control point refused"
        : !data->subscribe(true,
                           [this](NimBLERemoteCharacteristic*, uint8_t* bytes, size_t len, bool) {
                               onPmdData(bytes, len);
                           })
            ? "data refused"
            : nullptr;
    if (failed) {
        boardlog::printf("[hr] cannot subscribe to Polar measurements (%s, link %s): pulse only\n", failed,
                         client->getConnInfo().isEncrypted() ? "encrypted" : "not encrypted");
        return;
    }
    // subscribe() reports success when there is no CCCD to switch on, and then
    // nothing arrives: worth a line in case the strap seems silent.
    const NimBLEUUID cccd(static_cast<uint16_t>(0x2902));
    if (!control->getDescriptor(cccd)) boardlog::println("[hr] Polar control point without CCCD: no answers");
    if (!data->getDescriptor(cccd)) boardlog::println("[hr] Polar data without CCCD: no frames");

    // What the strap offers, for the file and the log.
    const NimBLEAttValue offer = control->readValue();
    push(strap::RecordType::PmdControl, offer.data(), offer.size());
    if (const auto features = pmd::parseFeatures(offer.data(), offer.size())) {
        boardlog::printf("[hr] Polar measurements on offer: %s\n", pmd::featureNames(*features).c_str());
    }

    pmdControl_ = control;
    const uint32_t now = millis();
    for (Stream& s : streams_) {
        s.lastFrameMs.store(now);  // the stall clock starts now
        s.gotFrame.store(false);
        s.startDue = true;
        s.whyQuiet = "not started yet";
        s.health = Stream::Health::Unknown;
    }
}

void HeartRateLink::sendStart(Stream& s, uint32_t now) {
    s.startDue = false;
    s.lastStartMs = now;
    const std::vector<uint8_t> cmd =
        s.type == pmd::Measurement::Ecg ? pmd::startEcgCommand() : pmd::startAccCommand();
    const uint32_t replies = s.startReplies.load();
    // A write with response is back within a connection interval or two; the
    // strap's answer follows on its own, as an indication.
    if (!pmdControl_->writeValue(cmd.data(), cmd.size(), true)) {
        s.whyQuiet = "the start command did not go through";
        return;
    }
    waiting_ = &s;
    waitingSinceMs_ = now;
    waitingReplies_ = replies;
}

void HeartRateLink::checkStart(uint32_t now) {
    Stream& s = *waiting_;
    const uint32_t replies = s.startReplies.load();
    if (replies >> 8 != waitingReplies_ >> 8) {
        pmd::Reply r;
        r.opCode = pmd::kOpStart;
        r.measurement = static_cast<uint8_t>(s.type);
        r.error = static_cast<uint8_t>(replies);
        s.whyQuiet = pmd::started(r) ? "started, yet no frames came" : pmd::errorText(r.error);
    } else if (since(now, waitingSinceMs_) >= kReplyTimeoutMs) {
        s.whyQuiet = "no answer to the start command";
    } else {
        return;  // still waiting
    }
    waiting_ = nullptr;
}

void HeartRateLink::watch(Stream& s, uint32_t now) {
    const uint32_t quietMs = since(now, s.lastFrameMs.load());
    if (s.gotFrame.load() && quietMs < kStallMs) {
        if (s.health != Stream::Health::Streaming) boardlog::printf("[hr] %s streaming\n", s.name);
        s.health = Stream::Health::Streaming;
    } else if (quietMs >= kStallMs && s.health != Stream::Health::Quiet) {
        if (s.health == Stream::Health::Streaming) {
            boardlog::printf("[hr] %s stopped; starting it again every minute\n", s.name);
        } else {
            boardlog::printf("[hr] %s not streaming: %s; trying again every minute\n", s.name, s.whyQuiet);
        }
        s.health = Stream::Health::Quiet;
    }
}

void HeartRateLink::loop() {
    if (!link_.connected()) {
        if (wasConnected_) note("disconnected");
        wasConnected_ = false;
        pairing_ = nullptr;
        pmdControl_ = nullptr;
        waiting_ = nullptr;
        return;
    }
    const uint32_t now = millis();
    if (pairing_) {
        const bool encrypted = pairing_->getConnInfo().isEncrypted();
        if (!encrypted && since(now, pairingSinceMs_) < kPairingWaitMs) return;
        const char* result = encrypted ? "paired, link encrypted" : "pairing did not finish";
        note(result);
        boardlog::printf("[hr] %s\n", result);
        NimBLEClient* client = pairing_;
        pairing_ = nullptr;
        setUpPmd(client);
        return;
    }
    if (!pmdControl_) return;
    if (waiting_) checkStart(now);
    for (Stream& s : streams_) {
        watch(s, now);
        const bool restartDue =
            s.health == Stream::Health::Quiet && since(now, s.lastStartMs) >= kRestartEveryMs;
        if (!waiting_ && (s.startDue || restartDue)) sendStart(s, now);
    }
}

void HeartRateLink::push(strap::RecordType type, const uint8_t* data, size_t len) {
    const uint32_t now = millis();
    portENTER_CRITICAL(&queueMux_);
    queue_.push(type, now, data, len);
    portEXIT_CRITICAL(&queueMux_);
}

void HeartRateLink::note(const char* text) {
    push(strap::RecordType::Note, reinterpret_cast<const uint8_t*>(text), strlen(text));
}

void HeartRateLink::drainRecords(uint32_t now, bool keep, const RecordSink& sink) {
    uint8_t record[strap::kMaxRecordSize];

    portENTER_CRITICAL(&queueMux_);
    const uint32_t lost = queue_.takeLost();
    size_t waiting = queue_.size();
    portEXIT_CRITICAL(&queueMux_);
    // Losses between walks only shorten the minute before the next one.
    if (keep && lost > 0) {
        const uint8_t count[4] = {static_cast<uint8_t>(lost), static_cast<uint8_t>(lost >> 8),
                                  static_cast<uint8_t>(lost >> 16), static_cast<uint8_t>(lost >> 24)};
        sink(record, strap::encodeRecord(strap::RecordType::Lost, now, count, sizeof(count), record));
    }

    // At most what was there at the start: the strap keeps adding meanwhile.
    for (; waiting > 0; --waiting) {
        size_t len = 0;
        bool taken = false;
        portENTER_CRITICAL(&queueMux_);
        if (!queue_.empty()) {
            const strap::Slot& s = queue_.front();
            if (keep) {
                len = strap::encodeRecord(s.type, s.uptimeMs, s.payload, s.length, record);
                taken = true;
            } else {
                taken = since(now, s.uptimeMs) > kPreRollMs;
            }
            if (taken) queue_.pop();
        }
        portEXIT_CRITICAL(&queueMux_);
        if (!taken) break;
        if (len > 0) sink(record, len);
    }
}

HeartRateLink::Snapshot HeartRateLink::snapshot() {
    portENTER_CRITICAL(&mux_);
    Snapshot copy = shared_;
    portEXIT_CRITICAL(&mux_);
    copy.connected = link_.connected();
    return copy;
}
