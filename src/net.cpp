#include "net.h"

#include <WiFi.h>

#include <atomic>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "src/secrets.h is missing: copy src/secrets.example.h to src/secrets.h and fill it in"
#endif

namespace net {

namespace {

// The timeout is split between this many attempts. Each one starts afresh with
// the radio switched off and on, in case the first one got stuck.
constexpr int kAttempts = 2;

// True while connect() runs; read from the Wi-Fi event task.
std::atomic<bool> connecting{false};
bool handlerAdded = false;

// Logs why the network refused or dropped the board while connecting. The core
// retries most of these on its own; the reason says why it had to.
void onDisconnected(arduino_event_id_t, arduino_event_info_t info) {
    const wifi_event_sta_disconnected_t& d = info.wifi_sta_disconnected;
    // ASSOC_LEAVE is the board's own disconnect().
    if (!connecting || d.reason == WIFI_REASON_ASSOC_LEAVE) return;
    Serial.printf("[wifi] refused: reason %u %s, signal %d dBm\n", d.reason,
                  WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(d.reason)), d.rssi);
}

// One attempt: radio on, join, wait. On failure the radio is off again.
bool attempt(uint32_t timeoutMs) {
    const uint32_t start = millis();
    WiFi.mode(WIFI_STA);
    WiFi.begin(secrets::kWifiSsid, secrets::kWifiPassword);
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) delay(200);
    if (WiFi.status() == WL_CONNECTED) return true;
    Serial.printf("[wifi] attempt timed out, status %d\n", static_cast<int>(WiFi.status()));
    disconnect();
    return false;
}

}  // namespace

bool connect(uint32_t timeoutMs) {
    if (strlen(secrets::kWifiSsid) == 0) {
        Serial.println("[wifi] no network in secrets.h");
        return false;
    }
    if (!handlerAdded) {
        WiFi.onEvent(onDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
        handlerAdded = true;
    }

    const uint32_t start = millis();
    connecting = true;
    bool ok = false;
    for (int i = 0; i < kAttempts && !ok; ++i) {
        const uint32_t spent = millis() - start;
        const uint32_t left = timeoutMs > spent ? timeoutMs - spent : 0;
        ok = attempt(i + 1 < kAttempts ? timeoutMs / kAttempts : left);
    }
    connecting = false;

    if (!ok) {
        Serial.println("[wifi] no connection");
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
