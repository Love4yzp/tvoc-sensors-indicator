# `main/ui` — low-level UI interaction & rendering layer

This module centralizes UI thread-safety rules and low-level rendering memory management to prevent freezes and memory exhaustion.

---

## 1. The Threading Model & Deadlock Prevention

Three contexts touch the UI:

| Context | Runs | Holds the LVGL lock? |
|---|---|---|
| **LVGL task** | `lv_timer_handler`, widget event callbacks (button/slider/list) | **Yes** — runs under `lvgl_port_lock` |
| **view_event loop** | `esp_event` handlers registered on `view_event_handle` | Takes it via `lv_port_sem_take/give` around widget access |
| **background tasks** | WiFi, sensor, MQTT models | No |

### The Deadlock Hazard
1. `lv_port_sem_take()` waits **forever** (`portMAX_DELAY`).
2. `view_event_handle` has a **single consumer** task and a bounded queue.

Posting with a block (`portMAX_DELAY`) to `view_event_handle`:
- **From LVGL task**: Lock is held while waiting for space in queue → loop task cannot take lock to drain queue → **permanent freeze**.
- **From loop task itself**: Blocks waiting on its own queue → **permanent self-deadlock**.

### The API (`main/ui/ui_event.h`, `main/ui/ui_defer.h`)

```c
/* Post a UI event — ALWAYS non-blocking (timeout 0). Safe from any context. */
esp_err_t ui_event_post(int32_t event_id, const void *data, size_t size);

/* Run cb(arg) on the LVGL task (lock already held — touch widgets directly). */
esp_err_t ui_defer(void (*cb)(void *arg), void *arg);
```

**Rules for application code:**
- **Never** call `esp_event_post_to(view_event_handle, ...)` from `*_view.c` / `*_screen.c`. Always use `ui_event_post()`.
- Background → UI widget updates: prefer `ui_defer()` over manual `lv_port_sem_take/give`.

---

## 2. LVGL 9.5 Rendering & Layer Memory Diagnosis

### Layer Allocation Infinite Loop
If LVGL runs out of memory for offscreen render layers, `taskLVGL` spins endlessly in `lv_malloc_core` / `lv_draw_layer_alloc_buf`, logging:
`Allocating layer buffer failed. Try later` / `couldn't allocate memory (N bytes)`.

### The `clip_corner` Trap
LVGL 9's default theme sets `clip_corner=true` on `lv_list`, `msgbox`, and `win` widgets.
This forces LVGL to render rounded corner strips (`width x radius`, ARGB8888, ~12 KB for a 420-wide list) through temporary offscreen layers on **every single scroll frame**.
- **Remedy**: When the container background matches the parent/page background, disable clip_corner:
  ```c
  lv_obj_set_style_clip_corner(obj, false, LV_PART_MAIN);
  ```
  (See `main/wifi/wifi_list_screen.c` for reference).

### PSRAM Overflow Pool (`main/ui/ui_mem_pool.c`)
LVGL's internal TLSF allocator uses a 64 KB internal RAM pool (`CONFIG_LV_MEM_SIZE_KILOBYTES=64`). `ui_mem_pool.c` dynamically adds a 256 KB PSRAM pool via `lv_mem_add_pool()`:
- The base buffer must be 16-byte aligned (`heap_caps_aligned_alloc(16, ...)`).
- TLSF caps any added pool at `LV_MEM_SIZE + LV_MEM_POOL_EXPAND_SIZE`, which requires `CONFIG_LV_MEM_POOL_EXPAND_SIZE_KILOBYTES=256` in `sdkconfig.defaults`.

---

## 3. Freeze Monitoring & Performance

- **`ui_freeze_mon.c`**: A hardware timer task heartbeat-monitors `taskLVGL` (8 s timeout). If the UI task stops responding (deadlocked or spinning), it triggers a panic to capture backtraces via UART (`CONFIG_ESP_COREDUMP_ENABLE_TO_UART=y`).
- **FPS / Jank Measurement**: Enable `CONFIG_LV_USE_SYSMON=y` + `CONFIG_LV_USE_PERF_MONITOR_LOG_MODE=y` to inspect FPS and render/flush latencies via serial logs.

---

## 4. Enforcement & Host Tests

```bash
# Guard check: prevents direct blocking posts to view_event_handle in UI code
python3 scripts/check_event_post_safety.py

# Host unit tests: validates non-blocking invariants under FreeRTOS + esp_event
./dev test
```
