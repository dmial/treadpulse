#include "touch.h"

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

#include <algorithm>

#include "board.h"
#include "board_log.h"

namespace touch {

namespace {

// The controller picks its I2C address from the interrupt line's level when it
// comes out of reset: low gives the first one.
constexpr uint8_t kAddress = 0x5D;
constexpr uint8_t kAddressAlt = 0x14;
constexpr uint32_t kI2cHz = 400000;

constexpr uint16_t kRegOutputMax = 0x8048;  // X max (2 bytes), Y max (2 bytes), little-endian
constexpr uint16_t kRegProductId = 0x8140;  // "911" and a zero
constexpr uint16_t kRegStatus = 0x814E;     // bit 7: a fresh report; bits 0-3: touch count
constexpr uint16_t kRegPoint1 = 0x8150;     // first touch: X, Y (2 bytes each, little-endian)
constexpr uint8_t kStatusReady = 0x80;

uint8_t address = 0;
// The resolution the controller reports in; mapped onto the panel's.
uint16_t outputW = board::kLcdWidth, outputH = board::kLcdHeight;

// The last report holds until the controller has a fresh one.
bool pressed = false;
int32_t lastX = 0, lastY = 0;

bool readRegs(uint16_t reg, uint8_t* buf, uint8_t n) {
    Wire.beginTransmission(address);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg & 0xFF));
    if (Wire.endTransmission() != 0) return false;
    if (Wire.requestFrom(address, n) != n) return false;
    for (uint8_t i = 0; i < n; ++i) buf[i] = static_cast<uint8_t>(Wire.read());
    return true;
}

void writeReg(uint16_t reg, uint8_t value) {
    Wire.beginTransmission(address);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg & 0xFF));
    Wire.write(value);
    Wire.endTransmission();
}

bool answers(uint8_t a) {
    Wire.beginTransmission(a);
    return Wire.endTransmission() == 0;
}

// Reset with the interrupt line low (address 0x5D), then let go of the line.
void reset() {
    pinMode(board::kTouchInt, OUTPUT);
    pinMode(board::kTouchRst, OUTPUT);
    digitalWrite(board::kTouchInt, LOW);
    digitalWrite(board::kTouchRst, LOW);
    delay(10);
    digitalWrite(board::kTouchRst, HIGH);
    delay(50);
    pinMode(board::kTouchInt, INPUT);
    delay(50);  // the controller's firmware starts
}

int32_t scale(uint16_t v, uint16_t from, int32_t to) {
    const int32_t p = from > 0 ? static_cast<int32_t>(uint32_t(v) * to / from) : v;
    return std::min<int32_t>(std::max<int32_t>(p, 0), to - 1);
}

// LVGL polls this every few tens of milliseconds.
void read(lv_indev_t*, lv_indev_data_t* data) {
    uint8_t status = 0;
    if (readRegs(kRegStatus, &status, 1) && (status & kStatusReady)) {
        const bool wasPressed = pressed;
        pressed = false;
        uint8_t p[4];
        if ((status & 0x0F) > 0 && readRegs(kRegPoint1, p, sizeof(p))) {
            lastX = scale(p[0] | p[1] << 8, outputW, board::kLcdWidth);
            lastY = scale(p[2] | p[3] << 8, outputH, board::kLcdHeight);
            pressed = true;
            if (!wasPressed) {
                Serial.printf("[touch] %ld,%ld\n", static_cast<long>(lastX), static_cast<long>(lastY));
            }
        }
        writeReg(kRegStatus, 0);  // the report is taken; the controller makes the next
    }
    data->point.x = lastX;
    data->point.y = lastY;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

}  // namespace

bool begin() {
    reset();
    Wire.begin(board::kTouchSda, board::kTouchScl, kI2cHz);
    if (answers(kAddress)) {
        address = kAddress;
    } else if (answers(kAddressAlt)) {
        address = kAddressAlt;
    } else {
        boardlog::println("[touch] no controller");
        return false;
    }

    uint8_t id[4] = {};
    uint8_t out[4] = {};
    readRegs(kRegProductId, id, sizeof(id));
    if (readRegs(kRegOutputMax, out, sizeof(out))) {
        const uint16_t w = out[0] | out[1] << 8, h = out[2] | out[3] << 8;
        if (w > 0 && h > 0) {
            outputW = w;
            outputH = h;
        }
    }
    boardlog::printf("[touch] GT%.3s at 0x%02X, %ux%u\n", reinterpret_cast<const char*>(id), address,
                     outputW, outputH);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read);
    return true;
}

}  // namespace touch
