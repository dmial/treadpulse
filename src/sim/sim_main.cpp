// Renders the dashboard on the computer into BMP files, to look at the layout
// without the board (env:sim):
//
//   pio run -e sim && .pio/build/sim/program <out dir>
//
// Writes one image per demo scene plus the message screen.
#include <lvgl.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#include "screen.h"
#include "screen_demo.h"
#include "screen_settings.h"

namespace {

std::vector<uint16_t> frame(screen::kWidth * screen::kHeight);

void flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
    const auto* src = reinterpret_cast<const uint16_t*>(px);
    const int w = lv_area_get_width(area);
    for (int y = area->y1; y <= area->y2; ++y) {
        const uint16_t* row = src + (y - area->y1) * w;
        std::copy(row, row + w, frame.begin() + y * screen::kWidth + area->x1);
    }
    lv_display_flush_ready(disp);
}

uint32_t tick() {
    static const auto start = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - start)
                                     .count());
}

void put(uint8_t* p, uint32_t v, int bytes) {
    for (int i = 0; i < bytes; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

bool writeBmp(const std::string& path) {
    const int w = screen::kWidth, h = screen::kHeight, row = w * 3;
    uint8_t header[54] = {'B', 'M'};
    put(header + 2, 54 + row * h, 4);
    put(header + 10, 54, 4);
    put(header + 14, 40, 4);
    put(header + 18, w, 4);
    put(header + 22, h, 4);
    put(header + 26, 1, 2);
    put(header + 28, 24, 2);
    put(header + 34, row * h, 4);

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fwrite(header, 1, sizeof(header), f);
    std::vector<uint8_t> line(row);
    for (int y = h - 1; y >= 0; --y) {  // BMP rows go bottom-up
        for (int x = 0; x < w; ++x) {
            const uint16_t c = frame[y * w + x];
            const uint8_t r = (c >> 11) & 0x1f, g = (c >> 5) & 0x3f, b = c & 0x1f;
            line[x * 3 + 0] = static_cast<uint8_t>(b << 3 | b >> 2);
            line[x * 3 + 1] = static_cast<uint8_t>(g << 2 | g >> 4);
            line[x * 3 + 2] = static_cast<uint8_t>(r << 3 | r >> 2);
        }
        fwrite(line.data(), 1, line.size(), f);
    }
    fclose(f);
    printf("%s\n", path.c_str());
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";

    // Days on the statistics page follow the local time zone, as on the board.
    setenv("TZ", userconfig::kTimezone, 1);
    tzset();

    lv_init();
    lv_tick_set_cb(tick);
    lv_display_t* disp = lv_display_create(screen::kWidth, screen::kHeight);
    static std::vector<uint8_t> buf(screen::kWidth * 40 * 2);
    lv_display_set_buffers(disp, buf.data(), nullptr, static_cast<uint32_t>(buf.size()),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush);

    screen::Dashboard dash(screenSettings());
    dash.build();
    // Each scene twice: today's ring in minutes, then in hours and minutes.
    for (const bool inHours : {false, true}) {
        dash.setTodayInHours(inHours);
        for (int i = 0; i < screen::demo::kSceneCount; ++i) {
            dash.update(screen::demo::scene(i, dailyGoalSeconds()));
            lv_refr_now(disp);
            const std::string name = std::to_string(i) + "_" + screen::demo::sceneName(i) +
                                     (inHours ? "_hours" : "") + ".bmp";
            if (!writeBmp(dir + "/" + name)) return 1;
        }
    }
    // The statistics page, as after a swipe.
    dash.setTodayInHours(false);
    for (int i = 0; i < screen::demo::kStatsSceneCount; ++i) {
        screen::Model m = screen::demo::scene(0, dailyGoalSeconds());
        m.walking = false;
        m.hasStats = true;
        m.stats = screen::demo::statsScene(i, dailyGoalSeconds());
        dash.update(m);
        dash.showPage(screen::Dashboard::Page::Stats);
        lv_refr_now(disp);
        if (!writeBmp(dir + "/10_stats_" + std::to_string(i) + ".bmp")) return 1;
        dash.showPage(screen::Dashboard::Page::Main);
    }
    dash.message("Connecting to Wi-Fi…", "then uploading walks");
    lv_refr_now(disp);
    return writeBmp(dir + "/9_message.bmp") ? 0 : 1;
}
