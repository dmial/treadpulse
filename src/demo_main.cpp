// Demo firmware (env:freenove_s3_5in_demo): the dashboard cycles through
// made-up scenes, to check the layout and the text sizes on the real panel.
// No Bluetooth, Wi-Fi or storage here, so the walks in flash stay untouched.
#ifdef UI_DEMO

#include <Arduino.h>

#include "display.h"
#include "screen.h"
#include "screen_demo.h"
#include "screen_settings.h"

namespace {

constexpr uint32_t kSceneMs = 6000;

screen::Dashboard* dash = nullptr;
int scene = 0;
uint32_t nextMs = 0;

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[boot] treadmill display: screen demo");
    if (!display::begin()) return;
    dash = new screen::Dashboard(screenSettings());
    dash->build();
}

void loop() {
    if (dash && static_cast<int32_t>(millis() - nextMs) >= 0) {
        nextMs = millis() + kSceneMs;
        if (scene < screen::demo::kSceneCount) {
            const uint32_t start = millis();
            dash->update(screen::demo::scene(scene, dailyGoalSeconds()));
            display::refreshNow();
            Serial.printf("[demo] %d %s, drawn in %lu ms\n", scene, screen::demo::sceneName(scene),
                          static_cast<unsigned long>(millis() - start));
        } else {
            dash->message("Connecting to Wi-Fi…", "then uploading walks");
            Serial.println("[demo] message");
        }
        scene = (scene + 1) % (screen::demo::kSceneCount + 1);
    }
    display::loop();
    delay(5);
}

#endif  // UI_DEMO
