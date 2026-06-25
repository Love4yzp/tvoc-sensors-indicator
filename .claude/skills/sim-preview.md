# sim-preview — Headless screenshot of the PC simulator

Invoke this skill to render the current 480×480 UI without a display, window focus, or human input.

The simulator has a headless mode: when `SIM_SCREENSHOT=<path.bmp>` is set, it
runs a ~1.3 s warm-up (so mock WiFi + the first 1 Hz sensor update fire and
LVGL lays out), calls `lv_snapshot_take` on the active screen, writes a BMP,
and exits on its own. The skill converts BMP → PNG via macOS `sips` (built-in,
no extra deps).

## Steps

1. **Build** (incremental; ~1 s if nothing changed):
   ```bash
   cmake -S sim -B sim/build && cmake --build sim/build -j4
   ```
   If the build fails, report the compiler errors and stop.

2. **Render** (the binary exits by itself after writing the BMP):
   ```bash
   SIM_SCREENSHOT=/tmp/sim_preview.bmp ./sim/build/sensecap_sim
   ```

3. **Convert** to PNG so the Read tool can display it:
   ```bash
   sips -s format png /tmp/sim_preview.bmp --out /tmp/sim_preview.png >/dev/null
   ```

4. **View**: use the `Read` tool on `/tmp/sim_preview.png`.

## Notes

- Run from the repo root (`/Users/spencer/conductor/repos/sensecap-indicator-ha`).
- An SDL window briefly opens (the LVGL SDL driver needs a registered display) and closes when the binary exits. Independent of focus — `lv_snapshot_take` reads the LVGL framebuffer, not the screen.
- Output is always 480×480 ARGB8888 (matches the real hardware LCD).
- To capture a different screen state, post events first (e.g. modify `sim/mock/mock_sensors.c` to inject specific values) and rebuild.
- The warm-up is 1300 ms — change `SCREENSHOT_WARMUP_MS` in `sim/lv_port_sim.c` if a slower transition needs more time.
