// Bluetooth link to a heart rate strap (e.g. Polar H10): finds a device
// advertising the Heart Rate Service, subscribes to Heart Rate Measurement and
// reconnects on its own. Runs next to the treadmill link.
//
// Everything the strap sends is also kept as records for the walk's strap.bin
// (lib/strap/strap_file.h): every heart rate notification as it came, and from
// a Polar strap its ECG and accelerometer streams (lib/strap/pmd.h), started on
// every connect and again whenever one stalls.
#pragma once

#include <Arduino.h>

#include <atomic>
#include <functional>

#include "ble_link.h"
#include "heart_rate.h"
#include "pmd.h"
#include "record_queue.h"

class HeartRateLink {
   public:
    // Plain data only: copied under a spinlock between the NimBLE task and loop().
    struct Snapshot {
        bool connected = false;
        uint32_t lastValidMs = 0;  // millis() of the last valid pulse reading
        bool hasValid = false;     // at least one valid reading since boot
        uint16_t bpm = 0;          // last valid reading
    };

    // Between walks the records of this last stretch wait in memory, so a
    // walk's strap.bin starts with the minute before the belt moved.
    static constexpr uint32_t kPreRollMs = 60 * 1000;

    // Connects to the first one found. Scanning and reconnects run from
    // BleLink::loopAll().
    void begin();
    Snapshot snapshot();

    // Starts the ECG and accelerometer streams, and again whenever one stalls,
    // without ever standing still to wait for the strap. Call from loop().
    void loop();

    // Hands the waiting records over, oldest first, encoded as in strap.bin.
    // With `keep` false nothing is handed over: records older than kPreRollMs
    // are dropped, newer ones wait for the next call.
    using RecordSink = std::function<void(const uint8_t* record, size_t len)>;
    void drainRecords(uint32_t now, bool keep, const RecordSink& sink);

   private:
    // One of Polar's measurements, kept running while the strap is connected.
    struct Stream {
        enum class Health { Unknown, Streaming, Quiet };

        const char* name;
        pmd::Measurement type;
        // Set on the NimBLE task.
        std::atomic<uint32_t> lastFrameMs{0};
        std::atomic<bool> gotFrame{false};       // since the strap connected
        std::atomic<uint32_t> startReplies{0};  // count << 8 | the last one's error code
        // loop() only.
        bool startDue = false;  // to start once the control point is free
        uint32_t lastStartMs = 0;
        const char* whyQuiet = "";  // how the last start went, for the log
        Health health = Health::Unknown;
    };

    void onNotify(const uint8_t* data, size_t len);      // NimBLE task
    void onPmdData(const uint8_t* data, size_t len);     // NimBLE task
    void onPmdControl(const uint8_t* data, size_t len);  // NimBLE task
    void onConnected(NimBLEClient* client);              // loop()
    void setUpPmd(NimBLEClient* client);                 // loop()
    // Writes the start command and returns; checkStart() picks up the strap's
    // answer on a later pass.
    void sendStart(Stream& s, uint32_t now);  // loop()
    void checkStart(uint32_t now);            // loop()
    // Logs a stream starting or stopping to send frames, once per change.
    void watch(Stream& s, uint32_t now);  // loop()
    void push(strap::RecordType type, const uint8_t* data, size_t len);  // any task
    void note(const char* text);                                         // any task

    BleLink link_{"hr", 0x180D /* Heart Rate */, 0x2A37 /* Heart Rate Measurement */};
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    Snapshot shared_;

    portMUX_TYPE queueMux_ = portMUX_INITIALIZER_UNLOCKED;
    strap::RecordQueue queue_;  // under queueMux_

    // A Polar strap being paired before its measurements are set up, and since
    // when. loop() only; dropped once the link is down.
    NimBLEClient* pairing_ = nullptr;
    uint32_t pairingSinceMs_ = 0;
    // Polar's control point: set on connect, dropped by loop() once the link is
    // down (the next connect deletes the object). loop() only.
    NimBLERemoteCharacteristic* pmdControl_ = nullptr;
    bool wasConnected_ = false;  // loop() only
    Stream streams_[2] = {{"ECG", pmd::Measurement::Ecg}, {"accelerometer", pmd::Measurement::Acc}};
    // The start command awaiting its answer (one at a time, as the strap
    // expects), when it went out and its stream's reply count then. loop() only.
    Stream* waiting_ = nullptr;
    uint32_t waitingSinceMs_ = 0;
    uint32_t waitingReplies_ = 0;
};
