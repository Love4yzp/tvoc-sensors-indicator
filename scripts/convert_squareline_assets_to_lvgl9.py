#!/usr/bin/env python3
"""
Convert SquareLine-exported RGB565A8 assets from LVGL 8's interleaved layout
to LVGL 9's planar layout, in-place.

LVGL 8 (and SquareLine output) stores per-pixel triplets:
    [RGB565_lo, RGB565_hi, A8] × (w·h)        stride = w·3

LVGL 9 expects "color array followed by alpha array" (planar):
    [RGB565_lo, RGB565_hi] × (w·h)            stride = w·2  (RGB plane)
    [A8] × (w·h)                              stride = w    (alpha plane)

Total byte count is identical (w·h·3); only the order changes.
LVGL 9 derives the alpha-plane stride as `header.stride / 2`, so we must
also rewrite `header.stride` from `w*3` to `w*2`.

Use this whenever assets are regenerated from SquareLine for an LVGL-9
firmware. Idempotent on a single file (skips if stride already w·2).

Usage:
    python3 scripts/convert_squareline_assets_to_lvgl9.py [path ...]
        (default: main/assets/ui_img_*.c)
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

HEADER_W_RE      = re.compile(r'\.header\.w\s*=\s*(\d+)')
HEADER_H_RE      = re.compile(r'\.header\.h\s*=\s*(\d+)')
HEADER_STRIDE_RE = re.compile(r'\.header\.stride\s*=\s*(\d+)')
DATA_ARRAY_RE    = re.compile(r'(_data\[\]\s*=\s*\{)(.*?)(\};)', re.DOTALL)
BYTE_RE          = re.compile(r'0[xX][0-9a-fA-F]{2}')


def convert_one(path: Path) -> str:
    text = path.read_text()
    mw, mh, ms = HEADER_W_RE.search(text), HEADER_H_RE.search(text), HEADER_STRIDE_RE.search(text)
    md = DATA_ARRAY_RE.search(text)
    if not (mw and mh and ms and md):
        return "skip: missing header fields or data array"
    w, h, stride = int(mw.group(1)), int(mh.group(1)), int(ms.group(1))
    if stride == w * 2:
        return f"skip: stride already {w*2} (already planar)"
    if stride != w * 3:
        return f"skip: unexpected stride={stride} (expected {w*3})"

    bytes_in = [int(b, 16) for b in BYTE_RE.findall(md.group(2))]
    expected = w * h * 3
    if len(bytes_in) != expected:
        return f"skip: {len(bytes_in)} bytes, expected {expected}"

    rgb = bytearray()
    alpha = bytearray()
    for i in range(0, expected, 3):
        rgb.append(bytes_in[i])
        rgb.append(bytes_in[i + 1])
        alpha.append(bytes_in[i + 2])
    planar = bytes(rgb) + bytes(alpha)

    # Re-emit the array body, 16 bytes per line, matching SquareLine's style.
    lines = []
    for i in range(0, len(planar), 16):
        chunk = planar[i:i + 16]
        lines.append("    " + ",".join(f"0x{b:02X}" for b in chunk) + ",")
    new_body = "\n" + "\n".join(lines) + "\n"

    new_text = text[:md.start(2)] + new_body + text[md.end(2):]
    new_text = HEADER_STRIDE_RE.sub(f".header.stride = {w * 2}", new_text)
    path.write_text(new_text.rstrip() + "\n")
    return f"ok: {w}x{h}, stride {stride}->{w*2}, {len(planar)} bytes"


def main(argv: list[str]) -> int:
    if argv:
        paths = [Path(p) for p in argv]
    else:
        paths = sorted(Path("main/assets").glob("ui_img_*.c"))
    if not paths:
        print("no input files", file=sys.stderr)
        return 1
    for p in paths:
        print(f"{p}: {convert_one(p)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
