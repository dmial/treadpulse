// The panel's touch controller (GT911) as LVGL's pointer, so LVGL objects get
// taps like clicks.
#pragma once

namespace touch {

// Resets the controller and registers it with LVGL. Call after display::begin()
// and before the speaker starts: the reset drives a pin the speaker uses
// (board.h). Returns false when the controller does not answer; the screen
// then works as before, without touch.
bool begin();

}  // namespace touch
