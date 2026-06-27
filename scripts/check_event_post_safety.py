#!/usr/bin/env python3
"""Guard against the LVGL-lock deadlock class.

Background
----------
UI code (button callbacks, view-event handlers) runs on the LVGL task while it
holds the esp_lvgl_port recursive lock, or on the single-consumer
`view_event_handle` event loop. `lv_port_sem_take()` == `lvgl_port_lock(0)`,
which waits *forever*.

If such code posts to `view_event_handle` with `portMAX_DELAY` and that queue is
full (e.g. the Wi-Fi connect bring-up burst), the post blocks forever:

  * from the LVGL task  -> the lvgl lock is never released -> whole UI freezes;
  * from the loop itself -> the only consumer is blocked  -> permanent self-deadlock.

Both were real freezes (scan freeze, connect freeze). The systematic fix is the
ui_event_post() layer (main/ui/ui_event.c): a single non-blocking post API that
UI code must use. This check fails CI if any LVGL-context file (`*_view.c` /
`*_screen.c`) calls esp_event_post_to(view_event_handle, …) directly instead of
ui_event_post(), so the unsafe pattern can never come back silently. The
framework file itself (main/ui/) is exempt.

Run: python3 scripts/check_event_post_safety.py
Exits non-zero on any violation.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MAIN = REPO / "main"

# Files whose code runs on the LVGL task (button/event callbacks) or builds UI.
UI_FILE_GLOBS = ("*_view.c", "*_screen.c")
# The framework layer is the one place allowed to touch the raw loop.
EXEMPT_DIRS = ("ui",)

POST_FN = "esp_event_post_to"
TARGET_LOOP = "view_event_handle"


def _iter_post_calls(text: str):
    """Yield (start_line, call_text) for each esp_event_post_to(...) call.

    Handles calls that span multiple lines by scanning to the balanced ')'.
    """
    for m in re.finditer(re.escape(POST_FN) + r"\s*\(", text):
        i = m.end()  # just past '('
        depth = 1
        while i < len(text) and depth:
            c = text[i]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            i += 1
        call = text[m.start():i]
        line = text.count("\n", 0, m.start()) + 1
        yield line, call


def check_file(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8", errors="replace")
    violations: list[str] = []
    for line, call in _iter_post_calls(text):
        if TARGET_LOOP in call:
            rel = path.relative_to(REPO)
            snippet = " ".join(call.split())
            if len(snippet) > 100:
                snippet = snippet[:97] + "..."
            violations.append(f"{rel}:{line}: raw post to {TARGET_LOOP}: {snippet}")
    return violations


def main() -> int:
    files: list[Path] = []
    for glob in UI_FILE_GLOBS:
        for f in MAIN.rglob(glob):
            if any(part in EXEMPT_DIRS for part in f.relative_to(MAIN).parts):
                continue
            files.append(f)
    files.sort()

    all_violations: list[str] = []
    for f in files:
        all_violations.extend(check_file(f))

    if all_violations:
        print("event-post safety check: FAIL")
        print(f"  UI code (*_view.c / *_screen.c) must use ui_event_post() and never call")
        print(f"  {POST_FN}({TARGET_LOOP}, ...) directly — a blocking post there freezes")
        print("  the UI (lvgl lock held forever / loop self-deadlock). See main/ui/ui_event.h.")
        print()
        for v in all_violations:
            print(f"  {v}")
        return 1

    print(f"event-post safety check: OK ({len(files)} UI files scanned)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
