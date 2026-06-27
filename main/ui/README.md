# `main/ui` — low-level UI interaction layer

This module exists because the app layer kept reintroducing the same class of
UI freeze. It centralizes the thread-safety rules so application code (views,
screens, button callbacks) cannot get them wrong.

## The threading model

Three kinds of task touch the UI:

| Context | Runs | Holds the lvgl lock? |
|---|---|---|
| **LVGL task** | `lv_timer_handler`, every widget event callback (button/slider/keyboard/list) | **Yes** — the whole callback runs under `lvgl_port_lock` |
| **view_event loop** | `esp_event` handlers registered on `view_event_handle` | takes it via `lv_port_sem_take/give` around widget access |
| **background tasks** | wifi / sensor / mqtt models | no |

Two hard rules follow:

1. `lv_port_sem_take()` == `lvgl_port_lock(0)` — it waits **forever**.
2. `view_event_handle` has a **single consumer** (its loop task) and a bounded
   queue.

So a **blocking** post (`portMAX_DELAY`) to `view_event_handle`:

- from the LVGL task → the lvgl lock is never released → **the whole screen
  freezes** (rendering and touch both die);
- from the loop task itself → it blocks on its own full queue with itself as the
  only consumer → **permanent self-deadlock**.

Both shipped as real freezes (Wi-Fi scan freeze, Wi-Fi connect freeze). The bug
was easy to reintroduce: it appeared in **8 call sites across 4 domains**.

## The API

```c
#include "ui_event.h"

/* Post a UI event — ALWAYS non-blocking. Safe from any context. */
esp_err_t ui_event_post(int32_t event_id, const void *data, size_t size);

/* Run cb(arg) on the LVGL task (lock already held — touch widgets directly,
 * don't re-lock). For background tasks updating the UI. */
esp_err_t ui_defer(void (*cb)(void *arg), void *arg);
```

- `ui_event_post` posts with timeout 0. A full queue drops the event
  (recoverable — a re-render/refresh follows, or the user repeats the action);
  it never blocks, so it can never deadlock the UI.
- `ui_event.c` is deliberately **LVGL-free** so it host-compiles for the unit
  test. `ui_defer` (which needs LVGL) lives in `ui_defer.c`.

## Rules for application code

- **Never** call `esp_event_post_to(view_event_handle, …)` from a `*_view.c` /
  `*_screen.c` file. Use `ui_event_post()`.
- Background → UI widget updates: prefer `ui_defer()` over manual
  `lv_port_sem_take/give`.

## Enforcement & tests (no hardware)

| What | How |
|---|---|
| Static guard — fails if UI code posts to `view_event_handle` directly | `scripts/check_event_post_safety.py` |
| Runtime invariant — `ui_event_post` never blocks on a full queue; loop drains | `test/host` (ESP-IDF Unity, linux target — real `esp_event` + FreeRTOS) |

Run everything with:

```bash
./dev test            # guards + protocol unit tests + ESP-IDF host unit tests
./dev test --no-host  # fast Python checks only (no ESP-IDF needed)
```
