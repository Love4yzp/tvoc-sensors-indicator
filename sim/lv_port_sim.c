/**
 * LVGL SDL2 port — macOS simulator.
 *
 * Single-thread design:
 *   lv_sdl_window_create()  — calls SDL_Init + sets lv_tick_set_cb(SDL_GetTicks)
 *   lv_timer_handler()      — drives LVGL + polls SDL events (mouse, keyboard, quit)
 *   lv_port_sem_take/give   — no-ops; safe because this is the only thread
 *
 * On SDL_QUIT (window closed), LVGL's SDL driver calls exit(0) directly.
 */
#include "lv_port_sim.h"
#include "lvgl.h"          /* pulls in lv_sdl_window.h, lv_sdl_mouse.h, etc. */
#include "mock/mock_sensors.h"
#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>        /* getenv */
#include <string.h>

#define SIM_WIDTH  480
#define SIM_HEIGHT 480
#define SIM_TICK_MS 5      /* lv_timer_handler period in ms */

/* Long enough for mock_sensors to post WiFi + first 1 Hz sensor update,
 * and for LVGL to lay out + render twice. */
#define SCREENSHOT_WARMUP_MS 1300

/* ── lv_port.h stubs — no-ops in single-thread sim ─────────────────────── */
void lv_port_sem_take(void) {}
void lv_port_sem_give(void) {}
void lv_port_init(void)     { lv_port_sim_init(); }

/* ── Init ────────────────────────────────────────────────────────────────── */
void lv_port_sim_init(void) {
    /* lv_sdl_window_create internally calls SDL_Init(SDL_INIT_VIDEO)
     * and registers lv_tick_set_cb(SDL_GetTicks).
     * No separate SDL_Init call needed. */
    lv_display_t *disp = lv_sdl_window_create(SIM_WIDTH, SIM_HEIGHT);
    if (!disp) {
        fprintf(stderr, "[sim] lv_sdl_window_create failed\n");
        return;
    }
    lv_sdl_window_set_title(disp, "SenseCap Indicator — Simulator");

    /* Wire up input devices */
    lv_sdl_mouse_create();
    lv_sdl_mousewheel_create();
    lv_sdl_keyboard_create();

    printf("[sim] window %dx%d created\n", SIM_WIDTH, SIM_HEIGHT);
}

/* ── Off-screen BMP writer (32-bit ARGB8888) ─────────────────────────────── */
/* Writes BMP byte-by-byte to avoid struct packing surprises.
 * LVGL ARGB8888 on little-endian = bytes B,G,R,A — matches BMP BI_RGB 32-bit.
 * BMP stores rows bottom-up, so we iterate y high → low. */
static void wr_u16_le(FILE *f, uint16_t v) {
    fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f);
}
static void wr_u32_le(FILE *f, uint32_t v) {
    fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f);
    fputc((v >> 16) & 0xff, f); fputc((v >> 24) & 0xff, f);
}
static int write_bmp_argb8888(const char *path, const lv_draw_buf_t *buf) {
    int w = buf->header.w;
    int h = buf->header.h;
    uint32_t stride = buf->header.stride;
    const uint8_t *data = buf->data;
    uint32_t row_bytes = (uint32_t)w * 4u;
    uint32_t img_size  = row_bytes * (uint32_t)h;

    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "[sim] cannot open %s for writing\n", path); return -1; }
    /* BITMAPFILEHEADER */
    fputc('B', f); fputc('M', f);
    wr_u32_le(f, 54 + img_size);   /* file size */
    wr_u16_le(f, 0); wr_u16_le(f, 0);
    wr_u32_le(f, 54);              /* pixel data offset */
    /* BITMAPINFOHEADER */
    wr_u32_le(f, 40);              /* header size */
    wr_u32_le(f, (uint32_t)w);
    wr_u32_le(f, (uint32_t)h);     /* positive → bottom-up */
    wr_u16_le(f, 1);               /* planes */
    wr_u16_le(f, 32);              /* bits per pixel */
    wr_u32_le(f, 0);               /* BI_RGB, no compression */
    wr_u32_le(f, img_size);
    wr_u32_le(f, 2835); wr_u32_le(f, 2835);  /* 72 DPI in pixels/m */
    wr_u32_le(f, 0); wr_u32_le(f, 0);
    /* Pixel rows, bottom-up */
    for (int y = h - 1; y >= 0; y--) {
        fwrite(data + (uint32_t)y * stride, 1, row_bytes, f);
    }
    fclose(f);
    return 0;
}

/* ── Headless screenshot mode ────────────────────────────────────────────── */
/* When SIM_SCREENSHOT=<path.bmp> is set, pump frames so mock data populates
 * and LVGL renders, snapshot the active screen, then return so main() exits.
 * The skill converts BMP → PNG via macOS `sips` (no extra deps). */
static void run_screenshot_mode(const char *path) {
    uint32_t start = SDL_GetTicks();
    while (SDL_GetTicks() - start < SCREENSHOT_WARMUP_MS) {
        mock_sensors_tick();
        lv_timer_handler();
        SDL_Delay(10);
    }
    const char *layer = getenv("SIM_SCREENSHOT_LAYER");
    lv_obj_t *target = (layer && strcmp(layer, "top") == 0) ? lv_layer_top() : lv_screen_active();
    lv_draw_buf_t *snap = lv_snapshot_take(target, LV_COLOR_FORMAT_ARGB8888);
    if (!snap) {
        fprintf(stderr, "[sim] lv_snapshot_take returned NULL\n");
        return;
    }
    if (write_bmp_argb8888(path, snap) == 0) {
        printf("[sim] screenshot saved: %s (%ux%u)\n",
               path, (unsigned)snap->header.w, (unsigned)snap->header.h);
    }
    lv_draw_buf_destroy(snap);
}

/* ── Main loop ───────────────────────────────────────────────────────────── */
void lv_port_sim_run(void) {
    const char *shot = getenv("SIM_SCREENSHOT");
    if (shot && *shot) {
        run_screenshot_mode(shot);
        return;     /* main() will lv_deinit and exit */
    }

    /* lv_timer_handler() calls lv_sdl_*_read() which polls SDL events.
     * SDL_QUIT is handled inside LVGL's driver (calls exit(0)).
     * lv_timer_handler() returns the time until the next timer fires (ms). */
    while (1) {
        mock_sensors_tick();        /* fire sensor/wifi events on schedule */
        uint32_t delay = lv_timer_handler();
        if (delay > SIM_TICK_MS) delay = SIM_TICK_MS;
        SDL_Delay(delay);
    }
}
