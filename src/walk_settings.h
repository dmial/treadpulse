// The walk tracker's settings, taken from user_config.h.
#pragma once

#include "walk_tracker.h"
#if __has_include("user_config.h")
#include "user_config.h"
#else
#error "src/user_config.h is missing: copy src/user_config.example.h to src/user_config.h and fill it in"
#endif

inline walk::Config walkSettings() {
    walk::Config c;
    c.steps.base = userconfig::kStepsPerMinuteBase;
    c.steps.perKmh = userconfig::kStepsPerMinutePerKmh;
    return c;
}
