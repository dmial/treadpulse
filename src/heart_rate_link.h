// Bluetooth link to a heart rate strap (e.g. Polar H10): finds a device
// advertising the Heart Rate Service, subscribes to Heart Rate Measurement and
// reconnects on its own. Runs next to the treadmill link.
#pragma once

#include <Arduino.h>

#include "ble_link.h"
#include "heart_rate.h"

class HeartRateLink {
   public:
    // Plain data only: copied under a spinlock between the NimBLE task and loop().
    struct Snapshot {
        bool connected = false;
        uint32_t lastValidMs = 0;  // millis() of the last valid pulse reading
        bool hasValid = false;     // at least one valid reading since boot
        uint16_t bpm = 0;          // last valid reading
    };

    // Connects to the first one found. Scanning and reconnects run from
    // BleLink::loopAll().
    void begin();
    Snapshot snapshot();

   private:
    void onNotify(const uint8_t* data, size_t len);

    BleLink link_{"hr", 0x180D /* Heart Rate */, 0x2A37 /* Heart Rate Measurement */};
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    Snapshot shared_;
};
