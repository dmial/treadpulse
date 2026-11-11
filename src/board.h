// Pin map of the Freenove ESP32-S3 5.0" IPS display (FNK0115Q).
// Source: Freenove_ESP32_S3_Display_FNK0115 examples (TFT_Display.h, music.h).
#pragma once

namespace board {

// 800x480 RGB parallel panel
constexpr int kLcdWidth = 800;
constexpr int kLcdHeight = 480;
constexpr int kBacklight = 2;
constexpr int kDe = 40, kVsync = 41, kHsync = 39, kPclk = 42;
constexpr int kR0 = 45, kR1 = 48, kR2 = 47, kR3 = 21, kR4 = 14;
constexpr int kG0 = 5, kG1 = 6, kG2 = 7, kG3 = 15, kG4 = 16, kG5 = 4;
constexpr int kB0 = 8, kB1 = 3, kB2 = 46, kB3 = 9, kB4 = 1;

// Speaker amplifier over I2S (BCLK shares GPIO0 with the BOOT button)
constexpr int kI2sBclk = 0, kI2sLrclk = 18, kI2sData = 17;

}  // namespace board
