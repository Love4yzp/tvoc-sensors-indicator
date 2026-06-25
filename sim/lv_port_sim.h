#pragma once

/* Initialize SDL2 window (480×800) and LVGL display + mouse indev. */
void lv_port_sim_init(void);

/* Block until the SDL2 window is closed, calling lv_timer_handler() each tick. */
void lv_port_sim_run(void);
