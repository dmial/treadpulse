// Wi-Fi on demand. Wi-Fi shares the radio with Bluetooth, so it stays off
// except for short sessions between walks (set the clock, upload walks).
// Network name and password come from src/secrets.h.
#pragma once

#include <Arduino.h>

namespace net {

// Connects to the home Wi-Fi. Returns false when no network is configured or
// it does not answer within the timeout (Wi-Fi is then switched off again).
bool connect(uint32_t timeoutMs);

// Disconnects and switches the Wi-Fi radio off.
void disconnect();

}  // namespace net
