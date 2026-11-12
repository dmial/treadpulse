// Bluetooth link to the treadmill: finds a device advertising the Fitness
// Machine Service, subscribes to Treadmill Data (and, when the treadmill has
// it, Fitness Machine Status) and reconnects on its own.
#pragma once

#include <Arduino.h>

#include "ble_link.h"
#include "ftms.h"

class TreadmillLink {
   public:
    // Plain data only: copied under a spinlock between the NimBLE task and loop().
    struct Snapshot {
        bool connected = false;
        bool hasData = false;       // at least one notification since boot
        uint32_t lastNotifyMs = 0;  // millis() of the last notification
        ftms::TreadmillState state;
        // Stops the treadmill reported since boot (stop pressed or safety key
        // pulled); it grows by one per stop.
        uint32_t stopCount = 0;
    };

    // Connects to the first one found. Scanning and reconnects run from
    // BleLink::loopAll().
    void begin();
    Snapshot snapshot();

   private:
    void onNotify(const uint8_t* data, size_t len);
    void onStatus(const uint8_t* data, size_t len);

    BleLink link_{"ble", 0x1826 /* Fitness Machine */, 0x2ACD /* Treadmill Data */};
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    Snapshot shared_;
    ftms::ZeroGlitchFilter glitchFilter_;  // used under mux_
};
