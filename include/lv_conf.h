// LVGL configuration for the treadmill display, shared by the board and the
// computer-side renderer (env:sim). Anything not set here keeps LVGL's default
// (see lv_conf_internal.h of LVGL 9.5).
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// Plain malloc: on the board, large blocks land in PSRAM by themselves.
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

// Every LVGL call comes from the Arduino loop task.
#define LV_USE_OS LV_OS_NONE

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

// No theme: the dashboard sets every style it uses.
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0

#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0

#endif  // LV_CONF_H
