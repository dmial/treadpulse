// Made-up scenes for looking at the dashboard without walking. The same scenes
// render on the computer (env:sim) and cycle on the board (env:freenove_s3_5in_demo).
#pragma once

#include "screen.h"

namespace screen {
namespace demo {

constexpr int kSceneCount = 8;

// Short file-friendly name of a scene, e.g. "walk".
const char* sceneName(int i);

// Scene `i` (0..kSceneCount-1) for a daily goal of `goalSeconds`.
Model scene(int i, uint32_t goalSeconds);

// The statistics page on 2026-11-12, local days as the process time zone has
// them: 0 = the history began yesterday (two days of walks), 1 = two full
// weeks with every kind of day, 2 = the history began today (no full day yet).
constexpr int kStatsSceneCount = 3;
stats::Window statsScene(int i, uint32_t goalSeconds);

}  // namespace demo
}  // namespace screen
