// The 800x480 RGB panel behind LVGL: panel timings, backlight, the LVGL
// display and its draw buffer.
#pragma once

namespace display {

// Panel, backlight and LVGL. Returns false when the panel does not start.
bool begin();

// Lets LVGL redraw what changed; call often from loop().
void loop();

// Draws pending changes right away, e.g. before a long blocking call.
void refreshNow();

}  // namespace display
