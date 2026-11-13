#include "beeper.h"

#include <ESP_I2S.h>

#include <cmath>

#include "board.h"
#include "board_log.h"

namespace {
constexpr uint32_t kSampleRate = 16000;
constexpr int16_t kAmplitude = 6000;  // well below full scale: the speaker is loud
I2SClass i2s;
}  // namespace

bool Beeper::begin() {
    i2s.setPins(board::kI2sBclk, board::kI2sLrclk, board::kI2sData);
    ok_ = i2s.begin(I2S_MODE_STD, kSampleRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    if (!ok_) boardlog::println("[beep] I2S init failed");
    return ok_;
}

void Beeper::tone(uint16_t freqHz, uint16_t durationMs) {
    if (!ok_) return;
    constexpr size_t kChunk = 256;
    int16_t buf[kChunk];
    const uint32_t total = kSampleRate * durationMs / 1000;
    const uint32_t fade = kSampleRate / 200;  // 5 ms ramps avoid clicks

    for (uint32_t done = 0; done < total;) {
        const size_t n = std::min<uint32_t>(kChunk, total - done);
        for (size_t i = 0; i < n; ++i) {
            const uint32_t k = done + i;
            float env = 1.0f;
            if (k < fade) env = static_cast<float>(k) / fade;
            else if (total - k < fade) env = static_cast<float>(total - k) / fade;
            const float phase = 2.0f * static_cast<float>(M_PI) * freqHz * k / kSampleRate;
            buf[i] = static_cast<int16_t>(kAmplitude * env * sinf(phase));
        }
        i2s.write(reinterpret_cast<const uint8_t*>(buf), n * sizeof(int16_t));
        done += n;
    }
    // Flush the DMA buffers with silence so the tone does not repeat.
    memset(buf, 0, sizeof(buf));
    for (int i = 0; i < 8; ++i) i2s.write(reinterpret_cast<const uint8_t*>(buf), sizeof(buf));
}
