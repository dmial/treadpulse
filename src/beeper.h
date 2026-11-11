// Short tones on the onboard speaker (I2S amplifier).
#pragma once

#include <Arduino.h>

class Beeper {
   public:
    bool begin();
    void tone(uint16_t freqHz, uint16_t durationMs);

    void linkLost() {  // two low beeps
        tone(660, 150);
        delay(80);
        tone(660, 150);
    }
    void linkRestored() { tone(1320, 100); }  // one short high beep

    // The pulse went over the base-walk ceiling (138): one longer middle beep.
    void pulseCeiling() { tone(990, 250); }
    // The pulse went over the interval line (156): two rising beeps.
    void pulseInterval() {
        tone(990, 120);
        delay(60);
        tone(1480, 180);
    }

   private:
    bool ok_ = false;
};
