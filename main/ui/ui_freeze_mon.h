#ifndef UI_FREEZE_MON_H
#define UI_FREEZE_MON_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the UI freeze monitor.
 *
 * Arms a 500 ms heartbeat timer on the LVGL task and a low-priority monitor
 * task. If the heartbeat stops for UI_FREEZE_TIMEOUT_MS the LVGL task is
 * either spinning or blocked while holding the LVGL lock (the whole UI is
 * dead: no touch, no redraw). The monitor then logs diagnostics, prints both
 * cores' backtraces, and aborts so the panic handler writes a UART coredump
 * (which contains the frozen taskLVGL stack) and reboots the device.
 *
 * Call once after lv_port_init().
 */
void ui_freeze_mon_start(void);

#ifdef __cplusplus
}
#endif

#endif
