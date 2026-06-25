/**
 * Simulator LVGL 9 configuration — SDL2 macOS build.
 * Hardware target uses its own sdkconfig-generated settings.
 *
 * LVGL requires "#if 1" at the top; do not change to #ifndef guard.
 */
#if 1

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   MEMORY ALIGNMENT
 *====================*/
/* Force 4-byte alignment on image data arrays (assets/*.c).
 * Without this, LVGL 9's lv_draw_buf_init logs "Data is not aligned" and
 * discards the pointer, rendering images as blank. */
#define LV_ATTRIBUTE_MEM_ALIGN __attribute__((aligned(4)))

/*====================
   COLOR SETTINGS
 *====================*/
/* 32-bit ARGB8888 — SDL2 native; avoids pixel-format conversion in the driver */
#define LV_COLOR_DEPTH 32

/*====================
   MEMORY SETTINGS
 *====================*/
/* Desktop sim: use the C-library allocator (effectively unlimited heap).
 * The builtin 512 KB pool is too small for a full-screen lv_snapshot_take
 * (480×480×4 ≈ 900 KB) used by the headless screenshot path. */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_MEM_SIZE          (512U * 1024U)  /* unused with CLIB allocator */

/*====================
   HAL SETTINGS
 *====================*/
#define LV_TICK_CUSTOM 0

/*====================
   LOGGING
 *====================*/
#define LV_USE_LOG      1
#define LV_LOG_LEVEL    LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF   1
#define LV_LOG_TIMESTAMP 1

/*====================
   DRIVERS
 *====================*/
/* SDL2 window + mouse input — built into LVGL 9 */
#define LV_USE_SDL               1
#define LV_SDL_INCLUDE_PATH      <SDL2/SDL.h>
#define LV_SDL_DIRECT_EXIT       1
#define LV_SDL_MOUSEWHEEL_MODE   LV_SDL_MOUSEWHEEL_MODE_ENCODER

/*====================
   WIDGETS
 *====================*/
#define LV_USE_LABEL     1
#define LV_USE_BTN       1   /* lv_button_create */
#define LV_USE_IMG       1
#define LV_USE_ARC       1
#define LV_USE_BAR       1
#define LV_USE_SLIDER    1
#define LV_USE_TABLE     1
#define LV_USE_DROPDOWN  1
#define LV_USE_ROLLER    1
#define LV_USE_TEXTAREA  1
#define LV_USE_MSGBOX    1
#define LV_USE_TILEVIEW  1
#define LV_USE_LIST      1
#define LV_USE_SWITCH    1
#define LV_USE_CHART     1
#define LV_USE_KEYBOARD  1
#define LV_USE_SPAN      0

/* Off-screen render to a buffer — used by the sim's headless screenshot path. */
#define LV_USE_SNAPSHOT  1

/*====================
   FONTS
 *====================*/
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_26 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT       &lv_font_montserrat_14

/*====================
   ANIMATIONS
 *====================*/
#define LV_USE_ANIMATION 1

/*====================
   INPUT DEVICES
 *====================*/
#define LV_USE_INDEV_TOUCHPAD 1
#define LV_USE_INDEV_BUTTON   0
#define LV_USE_INDEV_ENCODER  0

/*====================
   PERFORMANCE
 *====================*/
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR  0

#endif /* LV_CONF_H */
#endif /* #if 1 */
