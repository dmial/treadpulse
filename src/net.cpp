#include "net.h"

#include <WiFi.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "src/secrets.h is missing: copy src/secrets.example.h to src/secrets.h and fill it in"
#endif

namespace net {

bool connect(uint32_t timeoutMs) {
    if (strlen(secrets::kWifiSsid) == 0) {
        Serial.println("[wifi] no network in secrets.h");
        return false;
    }
    const uint32_t start = millis();
    WiFi.mode(WIFI_STA);
    WiFi.begin(secrets::kWifiSsid, secrets::kWifiPassword);
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) delay(200);
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[wifi] no connection");
        disconnect();
        return false;
    }
    Serial.printf("[wifi] connected in %lu ms\n", static_cast<unsigned long>(millis() - start));
    return true;
}

void disconnect() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

}  // namespace net
