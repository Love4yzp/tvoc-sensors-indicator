# Agent Guide

> **File roles (pinned — do not merge, delete, or "deduplicate" these two files):**
> - `CLAUDE.md` — read by **Claude Code** (it has no native fallback to AGENTS.md).
> - `AGENTS.md` (this file) — read by **other agents** (Codex, Cursor, Kimi Code, etc.); CLAUDE.md is ignored by them.
> Both are entry points on purpose. Keep project knowledge in CLAUDE.md and agent-agnostic operational rules here; each file must stay useful on its own.

See [CLAUDE.md](CLAUDE.md) for project architecture, build commands, and verification workflow. The rules below are project-specific operational knowledge — follow them, they are not suggestions.

## Build gotchas

- `main/CMakeLists.txt` collects sources via `file(GLOB_RECURSE)`, and the result is cached in `build/`. After adding, moving, or deleting any source file, run `touch main/CMakeLists.txt` before building — otherwise ninja keeps referencing stale paths and fails with `missing and no known rule to make it`.
- Never verify a build through `cmd 2>&1 | tail`: the pipeline exits with `tail`'s status and masks compile failures. Use `set -o pipefail` first (or check `${PIPESTATUS[0]}`).

## Crash & freeze debugging

- Decode watchdog/panic backtraces against the exact elf that was flashed:
  `~/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin/xtensa-esp32s3-elf-addr2line -e build/indicator_ha.elf -f -C -a <0x40/0x42 code addrs>`
  The top frames (`esp_crosscore_isr`, `_xt_lowint1`) are the dump mechanism, not the crash site. Two dumps seconds apart stuck in the same function means the task is spinning.
- A freeze with **no** watchdog output means a task is blocked (semaphore/queue/mutex with `portMAX_DELAY`), not spinning — the task WDT cannot catch blocked tasks. This is why `sdkconfig` ships `CONFIG_ESP_TASK_WDT_PANIC=y` (spin → auto-reboot) and `CONFIG_ESP_COREDUMP_ENABLE_TO_UART=y` (panic → every task's stack on serial). Decode saved dumps with `espcoredump.py info_corefile -c <dump> build/indicator_ha.elf`.
- `main/ui/ui_freeze_mon.c` heartbeat-watches taskLVGL (8 s timeout) and aborts on freeze, producing exactly such a coredump plus reboot. After any field UI freeze, capture the full serial log and decode the coredump before theorizing about root causes.

## Serial monitor & flash gotchas

- `idf.py monitor` requires a TTY and fails headless (`Monitor requires standard input to be attached to TTY`); macOS `script -q` also fails (`tcgetattr: Operation not supported on socket`). Working wrapper for background capture: `python3 -c "import pty; pty.spawn(['./dev','monitor'])"` (pty.spawn tolerates a non-TTY stdin).
- Stop any monitor before flashing — the port is exclusive. When multiple USB serial devices are attached (e.g. a DJI mic), port autodetect can pick the wrong one; pass it explicitly: `./dev flash -p /dev/cu.usbserial-XXXX`.

## sdkconfig editing gotchas

- `sdkconfig.defaults` is merged on **every** reconfigure, not just when `sdkconfig` is first created; unknown symbols there are dropped with a warning (visible in `idf.py reconfigure` output). Confirmed dead in LVGL 9.5: `LV_MEM_CUSTOM`, `LV_COLOR_SCREEN_TRANSP`, `LV_SPRINTF_USE_FLOAT`.
- Hand-edits to `sdkconfig` in a non-canonical position can be silently dropped on the next reconfigure. Reliable workflow: put the symbol in `sdkconfig.defaults` (or at its canonical menu position in `sdkconfig`), run `idf.py reconfigure`, then verify it landed in `build/config/sdkconfig.h` before building.

## LVGL rendering diagnosis (9.5)

- Layer-alloc freeze class: taskLVGL spins in `lv_malloc_core` / `lv_draw_layer_alloc_buf`. Confirm by enabling `CONFIG_LV_USE_LOG=y` + `CONFIG_LV_LOG_LEVEL_INFO=y` + `CONFIG_LV_LOG_PRINTF=y` and looking for the repeating triplet `Allocating layer buffer failed. Try later` / `couldn't allocate memory (N bytes)` / `No memory: WxH, cf: F, stride: S, BByte`. The `WxH` and `cf` (16 = ARGB8888) identify the layer; `Layer memory used: X kB` lines show successful ones. Turn these off again afterwards — INFO level is noisy.
- Known trigger here: the default theme sets `clip_corner=true` on `lv_list` backgrounds (also msgbox/win), which makes LVGL render the top/bottom rounded strips (`width x radius`, ARGB8888, ~12 KB for 420-wide) through offscreen layers on every scroll frame. If the rounded corner is invisible (container bg matches the page bg), kill it: `lv_obj_set_style_clip_corner(obj, false, LV_PART_MAIN)`. Reference: `main/wifi/wifi_list_screen.c`.
- LVGL's builtin TLSF pool is 64 KB internal RAM (`CONFIG_LV_MEM_SIZE_KILOBYTES=64`) and a layer allocation that can never succeed is retried forever = frozen UI. `main/ui/ui_mem_pool.c` adds a 256 KB PSRAM overflow pool via `lv_mem_add_pool()`; two non-obvious requirements: the pool base must be 8-byte aligned (`heap_caps_aligned_alloc(16, ...)` — plain `heap_caps_malloc` only guarantees 4), and any pool is capped at `LV_MEM_SIZE + LV_MEM_POOL_EXPAND_SIZE`, hence `CONFIG_LV_MEM_POOL_EXPAND_SIZE_KILOBYTES=256` in sdkconfig.defaults.
- UI jank measurement: enable `CONFIG_LV_USE_SYSMON=y` + `CONFIG_LV_USE_PERF_MONITOR=y` + `CONFIG_LV_USE_PERF_MONITOR_LOG_MODE=y` to get periodic `sysmon: X FPS ... refr Yms (render Zms | flush Wms), CPU N%` lines on serial — no need to read the on-screen overlay. Reference numbers (480×480 RGB, full refresh): idle render ~0 ms @93 FPS; an on-screen-keyboard keystroke costs ~130 ms render (brief 6-7 FPS dips) — inherent to SW rendering, not a bug.

## UI conventions

- On-screen keyboard must never cover the field being edited: shrink the scrollable form so its bottom edge sits above the keyboard and `lv_obj_scroll_to_view_recursive()` the focused field; restore full height when the keyboard hides. Reference implementation: `_set_keyboard_visible()` in `main/ha/ha_config.c`. Reuse this pattern for any new form with text input.
