#include "display.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <hal/lcd_ll.h>
#include <lvgl.h>
#include <soc/gdma_channel.h>
#include <soc/gdma_struct.h>
#include <soc/soc_caps.h>

#include "board.h"

// ---------------------------------------------------------------- flash guard
//
// The panel's DMA streams the screen out of two small bounce buffers in internal
// RAM, and the panel's interrupts refill them from the frame buffer in PSRAM,
// which is reached through the cache. The core runs from PSRAM (custom_sdkconfig
// in platformio.ini), so flash writes leave the cache on. A few rare operations
// (mapping flash, e.g. reading the partition table) still switch it off, and a
// refill at that moment would crash the board. The linker sends the cache off/on
// functions through these wrappers (-Wl,--wrap in platformio.ini), which hold the
// panel's two interrupts off meanwhile: VSYNC, and the DMA's "buffer sent" (EOF).
// At worst the screen glitches for a frame; the next VSYNC restarts it.
extern "C" {
void __real_spi_flash_disable_interrupts_caches_and_other_cpu(void);
void __real_spi_flash_enable_interrupts_caches_and_other_cpu(void);

static portMUX_TYPE s_guardLock = portMUX_INITIALIZER_UNLOCKED;
static DRAM_ATTR int s_lcdDmaChannel = -1;  // set once the panel is running
static DRAM_ATTR int s_cacheOffDepth = 0;
static DRAM_ATTR uint32_t s_vsyncWasOn = 0;
static DRAM_ATTR uint32_t s_eofWasOn = 0;

void IRAM_ATTR __wrap_spi_flash_disable_interrupts_caches_and_other_cpu(void) {
    portENTER_CRITICAL(&s_guardLock);
    if (s_cacheOffDepth++ == 0) {
        s_vsyncWasOn = LCD_CAM.lc_dma_int_ena.val & LCD_LL_EVENT_VSYNC_END;
        LCD_CAM.lc_dma_int_ena.val &= ~static_cast<uint32_t>(LCD_LL_EVENT_VSYNC_END);
        if (s_lcdDmaChannel >= 0) {
            s_eofWasOn = GDMA.channel[s_lcdDmaChannel].out.int_ena.out_eof;
            GDMA.channel[s_lcdDmaChannel].out.int_ena.out_eof = 0;
        }
    }
    portEXIT_CRITICAL(&s_guardLock);
    __real_spi_flash_disable_interrupts_caches_and_other_cpu();
}

void IRAM_ATTR __wrap_spi_flash_enable_interrupts_caches_and_other_cpu(void) {
    __real_spi_flash_enable_interrupts_caches_and_other_cpu();
    portENTER_CRITICAL(&s_guardLock);
    if (--s_cacheOffDepth == 0) {
        if (s_lcdDmaChannel >= 0 && s_eofWasOn) {
            GDMA.channel[s_lcdDmaChannel].out.int_ena.out_eof = 1;
        }
        if (s_vsyncWasOn) LCD_CAM.lc_dma_int_ena.val |= LCD_LL_EVENT_VSYNC_END;
    }
    portEXIT_CRITICAL(&s_guardLock);
}
}

namespace display {

namespace {

// LVGL renders the screen in stripes of this many lines (in internal RAM),
// then copies each stripe into the panel's frame buffer (in PSRAM).
constexpr int kBufferLines = 24;

// Each of the panel's two bounce buffers holds this many lines (16 KB). When the
// DMA read the frame buffer in PSRAM directly, it ran dry for a moment whenever
// a flash write held the bus that flash and PSRAM share: a visible flicker.
constexpr int kBounceLines = 10;

Arduino_RGB_Display* gfx = nullptr;
lv_display_t* disp = nullptr;

Arduino_ESP32RGBPanel* makePanel() {
    using namespace board;
    // Timings for the 5.0" IPS panel, taken from Freenove's FNK0115Q example.
    return new Arduino_ESP32RGBPanel(kDe, kVsync, kHsync, kPclk,
                                     kR0, kR1, kR2, kR3, kR4,
                                     kG0, kG1, kG2, kG3, kG4, kG5,
                                     kB0, kB1, kB2, kB3, kB4,
                                     0 /* hsync polarity */, 4 /* front porch */,
                                     4 /* pulse width */, 8 /* back porch */,
                                     0 /* vsync polarity */, 4 /* front porch */,
                                     4 /* pulse width */, 8 /* back porch */,
                                     1 /* pclk active neg */, 13000000 /* pclk Hz */,
                                     false /* big endian */, 0 /* de idle high */,
                                     0 /* pclk idle high */, kLcdWidth * kBounceLines);
}

// The GDMA transmit channel that feeds the panel, for the flash guard.
int findPanelDmaChannel() {
    for (int ch = 0; ch < SOC_GDMA_PAIRS_PER_GROUP_MAX; ++ch) {
        if (GDMA.channel[ch].out.peri_sel.sel == SOC_GDMA_TRIG_PERIPH_LCD0) return ch;
    }
    return -1;
}

void flush(lv_display_t* d, const lv_area_t* area, uint8_t* px) {
    if (gfx) {
        gfx->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t*>(px),
                                lv_area_get_width(area), lv_area_get_height(area));
    }
    lv_display_flush_ready(d);
}

uint32_t tick() { return millis(); }

}  // namespace

bool begin() {
    // LVGL comes up even without a panel, so the rest of the firmware (logging
    // walks) keeps working on a board whose screen fails.
    lv_init();
    lv_tick_set_cb(tick);
    disp = lv_display_create(board::kLcdWidth, board::kLcdHeight);
    const size_t bytes = board::kLcdWidth * kBufferLines * sizeof(uint16_t);
    void* buf = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) buf = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    lv_display_set_buffers(disp, buf, nullptr, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush);

    Arduino_RGB_Display* panel =
        new Arduino_RGB_Display(board::kLcdWidth, board::kLcdHeight, makePanel(), 0, true);
    if (!panel->begin()) {
        Serial.println("[display] panel init failed");
        return false;
    }
    s_lcdDmaChannel = findPanelDmaChannel();
    if (s_lcdDmaChannel < 0) Serial.println("[display] panel DMA channel not found");
    gfx = panel;
    pinMode(board::kBacklight, OUTPUT);
    digitalWrite(board::kBacklight, HIGH);
    gfx->fillScreen(0x0000);
    return true;
}

void loop() { lv_timer_handler(); }

void refreshNow() {
    if (disp) lv_refr_now(disp);
}

}  // namespace display
