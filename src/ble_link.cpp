#include "ble_link.h"

#include <array>

namespace {

constexpr uint32_t kScanDurationMs = 5000;
constexpr uint32_t kPauseBetweenScansMs = 1000;
// While another link is up, scan less often so its connection keeps most of the radio.
constexpr uint32_t kPauseBetweenScansConnectedMs = 5000;
constexpr uint32_t kRetryAfterFailureMs = 3000;
constexpr uint32_t kConnectTimeoutMs = 5000;

constexpr size_t kMaxLinks = 2;  // treadmill + heart rate strap
std::array<BleLink*, kMaxLinks> gLinks{};
size_t gLinkCount = 0;
// Unsigned "time since" comparisons stay correct when millis() wraps after 49 days.
uint32_t gLastScanStartMs = 0;
uint32_t gScanCycleMs = 0;  // scan duration + pause after it

class ScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        for (size_t i = 0; i < gLinkCount; ++i) gLinks[i]->onAdvertisement(dev);
    }
};

ScanCallbacks scanCallbacks;

}  // namespace

BleLink::BleLink(const char* tag, uint16_t serviceUuid, uint16_t characteristicUuid)
    : tag_(tag), service_(serviceUuid), characteristic_(characteristicUuid) {}

void BleLink::listenAlso(uint16_t characteristicUuid, NotifyHandler onNotify) {
    optional_.push_back(Optional{NimBLEUUID(characteristicUuid), std::move(onNotify)});
}

void BleLink::begin(NotifyHandler onNotify) {
    onNotify_ = std::move(onNotify);

    if (gLinkCount == 0) {
        NimBLEDevice::init("TreadmillDisplay");
        NimBLEScan* scan = NimBLEDevice::getScan();
        scan->setScanCallbacks(&scanCallbacks, false);
        scan->setActiveScan(true);  // the device name usually arrives in the scan response
        scan->setInterval(100);
        scan->setWindow(100);
    }
    if (gLinkCount < kMaxLinks) gLinks[gLinkCount++] = this;
}

bool BleLink::needsDevice() const {
    if (connected_.load() || found_.load()) return false;
    return !failed_.load() || millis() - failedAtMs_.load() >= kRetryAfterFailureMs;
}

void BleLink::onAdvertisement(const NimBLEAdvertisedDevice* dev) {
    if (!needsDevice()) return;
    if (!dev->isAdvertisingService(service_)) return;

    foundAddress_ = dev->getAddress();
    foundName_ = dev->getName();
    found_.store(true);
    NimBLEDevice::getScan()->stop();  // connect right away
}

void BleLink::onDisconnected(int reason) {
    Serial.printf("[%s] disconnected, reason %d\n", tag_, reason);
    connected_.store(false);
}

void BleLink::loopAll() {
    // Connect to what the last scan found, one link per call: connecting
    // blocks loop() for up to kConnectTimeoutMs.
    for (size_t i = 0; i < gLinkCount; ++i) {
        BleLink* link = gLinks[i];
        // A disconnect that raced with subscribing would leave the flag stuck.
        if (link->connected() && !link->client_->isConnected()) link->connected_.store(false);
    }
    for (size_t i = 0; i < gLinkCount; ++i) {
        BleLink* link = gLinks[i];
        if (!link->found_.load()) continue;
        const bool ok = link->connect();
        link->failedAtMs_.store(millis());
        link->failed_.store(!ok);
        link->found_.store(false);
        return;
    }

    bool anyNeedsDevice = false;
    bool anyConnected = false;
    for (size_t i = 0; i < gLinkCount; ++i) {
        anyNeedsDevice = anyNeedsDevice || gLinks[i]->needsDevice();
        anyConnected = anyConnected || gLinks[i]->connected();
    }
    if (!anyNeedsDevice) return;

    NimBLEScan* scan = NimBLEDevice::getScan();
    if (scan->isScanning() || millis() - gLastScanStartMs < gScanCycleMs) return;

    scan->clearResults();
    scan->start(kScanDurationMs, false);
    gLastScanStartMs = millis();
    gScanCycleMs = kScanDurationMs +
                   (anyConnected ? kPauseBetweenScansConnectedMs : kPauseBetweenScansMs);
}

bool BleLink::connect() {
    Serial.printf("[%s] connecting to %s (%s)\n", tag_, foundName_.c_str(),
                  foundAddress_.toString().c_str());

    if (!client_) {
        client_ = NimBLEDevice::createClient();
        client_->setClientCallbacks(&clientCallbacks_, false);
        client_->setConnectTimeout(kConnectTimeoutMs);
    }
    if (!client_->connect(foundAddress_)) {
        Serial.printf("[%s] connect failed (busy with another app?)\n", tag_);
        return false;
    }

    NimBLERemoteService* service = client_->getService(service_);
    NimBLERemoteCharacteristic* chr = service ? service->getCharacteristic(characteristic_) : nullptr;
    const bool subscribed =
        chr && chr->canNotify() &&
        chr->subscribe(true, [this](NimBLERemoteCharacteristic*, uint8_t* bytes, size_t len, bool) {
            if (onNotify_) onNotify_(bytes, len);
        });
    if (!subscribed) {
        Serial.printf("[%s] no %s characteristic, disconnecting\n", tag_,
                      characteristic_.toString().c_str());
        client_->disconnect();
        return false;
    }

    subscribeOptional(service);

    deviceName_ = foundName_;
    connected_.store(true);
    Serial.printf("[%s] subscribed to %s\n", tag_, deviceName_.c_str());
    return true;
}

void BleLink::subscribeOptional(NimBLERemoteService* service) {
    for (const Optional& o : optional_) {
        NimBLERemoteCharacteristic* chr = service->getCharacteristic(o.characteristic);
        const NotifyHandler handler = o.onNotify;
        const bool subscribed =
            chr && chr->canNotify() &&
            chr->subscribe(true, [handler](NimBLERemoteCharacteristic*, uint8_t* bytes, size_t len,
                                           bool) { handler(bytes, len); });
        Serial.printf("[%s] %s %s\n", tag_, subscribed ? "also subscribed to" : "no optional",
                      o.characteristic.toString().c_str());
    }
}
