// Bluetooth LE client link: finds a device advertising a given service,
// subscribes to one of its notify characteristics (plus optional ones) and
// reconnects on its own. All links share one scanner, so the treadmill and the
// heart rate strap are found and kept connected at the same time.
#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

class BleLink {
   public:
    // Runs on the NimBLE host task.
    using NotifyHandler = std::function<void(const uint8_t* data, size_t len)>;

    // tag: short name for the serial log, e.g. "ble" or "hr".
    BleLink(const char* tag, uint16_t serviceUuid, uint16_t characteristicUuid);

    // Also subscribes to this characteristic of the same service when the
    // device has it; a device without it still connects. Call before begin().
    void listenAlso(uint16_t characteristicUuid, NotifyHandler onNotify);

    // Connects to the first device found that advertises the service.
    void begin(NotifyHandler onNotify);

    bool connected() const { return connected_.load(); }
    // Name of the connected device. Read it from loop() only.
    const std::string& deviceName() const { return deviceName_; }

    // Call often from loop(): drives the shared scanner and connects every link.
    static void loopAll();

    // Called from NimBLE callbacks (host task).
    void onAdvertisement(const NimBLEAdvertisedDevice* dev);
    void onDisconnected(int reason);

   private:
    class ClientCallbacks : public NimBLEClientCallbacks {
       public:
        explicit ClientCallbacks(BleLink* link) : link_(link) {}
        void onDisconnect(NimBLEClient*, int reason) override { link_->onDisconnected(reason); }

       private:
        BleLink* link_;
    };

    struct Optional {
        NimBLEUUID characteristic;
        NotifyHandler onNotify;
    };

    bool needsDevice() const;
    bool connect();
    void subscribeOptional(NimBLERemoteService* service);

    const char* tag_;
    NimBLEUUID service_;
    NimBLEUUID characteristic_;
    NotifyHandler onNotify_;
    std::vector<Optional> optional_;  // filled before begin(), read-only after
    std::string deviceName_;

    std::atomic<bool> connected_{false};
    // Written by the scan callback before found_ is set; read by loop() after.
    std::atomic<bool> found_{false};
    NimBLEAddress foundAddress_;
    std::string foundName_;
    // After a failed connect, ignore adverts for a while.
    std::atomic<bool> failed_{false};
    std::atomic<uint32_t> failedAtMs_{0};

    ClientCallbacks clientCallbacks_{this};
    NimBLEClient* client_ = nullptr;
};
