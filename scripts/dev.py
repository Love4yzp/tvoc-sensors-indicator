#!/usr/bin/env python3
"""Single front door for building/flashing the SenseCAP Indicator firmware.

Two independent toolchains live behind one CLI:

  * ESP32-S3 (default)  -> ESP-IDF `idf.py`.
  * RP2040 coprocessor  -> PlatformIO `pio` (thin passthrough run in rp2040/).

This module is a thin proxy and does NOT activate any environment itself.
The repo-root `./dev` launcher handles ESP-IDF activation from $IDF_PATH; when
invoking this script directly, run it inside an already-activated ESP-IDF
shell. The ensure_idf()/ensure_pio() guards below fail fast with a hint if the
required tool is missing.

Usage (via the repo-root launcher):
    ./dev build                (Windows:  dev build)       # ESP32-S3
    ./dev build --no-clean
    ./dev fullclean
    ./dev flash -p /dev/ttyACM0
    ./dev monitor

    ./dev rp2040 build         # RP2040 (alias: ./dev rp ...)
    ./dev rp2040 upload
    ./dev rp2040 monitor
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = ROOT / "build"
RP2040_DIR = ROOT / "rp2040"
PYTHON_ENV_MISMATCH_MARKERS = (
    "is currently active in the environment while the project was configured with",
    "Run 'idf.py fullclean' to start again",
)


# --- environment guards (per target) ----------------------------------------

def ensure_idf() -> None:
    """Exit with a clear hint if the ESP-IDF environment is not active."""
    if shutil.which("idf.py"):
        return
    if sys.platform.startswith("win"):
        hint = r"%IDF_PATH%\export.bat   (or use the ESP-IDF PowerShell/CMD)"
    else:
        hint = '. "$IDF_PATH/export.sh"   (or: get_idf)'
    print(
        "x ESP-IDF is not active: `idf.py` was not found on PATH.\n"
        f"  Activate it first, then re-run:\n    {hint}",
        file=sys.stderr,
    )
    raise SystemExit(127)


def ensure_pio() -> None:
    """Exit with a clear hint if PlatformIO is not installed."""
    if shutil.which("pio"):
        return
    print(
        "x PlatformIO not found: `pio` was not found on PATH.\n"
        "  Install it: https://platformio.org/install/cli   (or: pip install platformio)",
        file=sys.stderr,
    )
    raise SystemExit(127)


# --- runners -----------------------------------------------------------------

def run_idf(idf_args: list[str]) -> int:
    cmd = ["idf.py", *idf_args]
    print("$ " + " ".join(cmd), flush=True)
    return subprocess.run(cmd, cwd=ROOT).returncode


def run_idf_capture(idf_args: list[str]) -> subprocess.CompletedProcess[str]:
    cmd = ["idf.py", *idf_args]
    print("$ " + " ".join(cmd), flush=True)
    proc = subprocess.Popen(
        cmd,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=1,
    )
    output: list[str] = []
    assert proc.stdout is not None
    for line in proc.stdout:
        output.append(line)
        print(line, end="", flush=True)
    returncode = proc.wait()
    return subprocess.CompletedProcess(cmd, returncode, "".join(output), "")


def run_pio(pio_args: list[str]) -> int:
    cmd = ["pio", *pio_args]
    print("$ " + " ".join(cmd) + "   # (in rp2040/)", flush=True)
    return subprocess.run(cmd, cwd=RP2040_DIR).returncode


# --- ESP32-S3 (default) ------------------------------------------------------

def cmd_build(args: argparse.Namespace) -> int:
    # Clean by default: wipe build/ before building (preserves prior behavior).
    # --no-clean opts into a faster incremental build.
    if not args.no_clean and BUILD_DIR.exists():
        print(f"$ rm -rf {BUILD_DIR}", flush=True)
        shutil.rmtree(BUILD_DIR)
    return run_idf(["build"])


def cmd_fullclean(args: argparse.Namespace) -> int:
    del args
    return run_idf(["fullclean"])


def is_idf_python_env_mismatch(output: str) -> bool:
    return all(marker in output for marker in PYTHON_ENV_MISMATCH_MARKERS)


def cmd_flash(args: argparse.Namespace) -> int:
    idf_args: list[str] = []
    if args.port:
        idf_args += ["-p", args.port]
    idf_args += ["-b", str(args.baud)]
    flash_args = [*idf_args, "flash"]
    result = run_idf_capture(flash_args)
    if result.returncode == 0:
        return 0

    output = f"{result.stdout or ''}{result.stderr or ''}"
    if not is_idf_python_env_mismatch(output):
        return result.returncode

    print(
        "! ESP-IDF Python environment changed; running `idf.py fullclean` "
        "and retrying flash once.",
        file=sys.stderr,
        flush=True,
    )
    clean_result = run_idf_capture(["fullclean"])
    if clean_result.returncode != 0:
        return clean_result.returncode
    return run_idf(flash_args)


def cmd_monitor(args: argparse.Namespace) -> int:
    idf_args: list[str] = []
    if args.port:
        idf_args += ["-p", args.port]
    return run_idf([*idf_args, "monitor"])


# --- RP2040 (PlatformIO passthrough) -----------------------------------------

def cmd_rp_build(args: argparse.Namespace) -> int:
    return run_pio(["run"])


def cmd_rp_upload(args: argparse.Namespace) -> int:
    pio_args = ["run", "-t", "upload"]
    if args.port:
        pio_args += ["--upload-port", args.port]
    return run_pio(pio_args)


def cmd_rp_monitor(args: argparse.Namespace) -> int:
    pio_args = ["device", "monitor"]
    if args.port:
        pio_args += ["-p", args.port]
    return run_pio(pio_args)


# --- CLI ---------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="dev",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = parser.add_subparsers(dest="command", metavar="<command>")

    # ESP32-S3 (default target, top-level commands)
    p_build = sub.add_parser("build", help="[S3] clean (default) and build")
    p_build.add_argument(
        "--no-clean", action="store_true",
        help="incremental build; skip wiping build/ first",
    )
    p_build.set_defaults(func=cmd_build, ensure=ensure_idf)

    p_fullclean = sub.add_parser("fullclean", help="[S3] run idf.py fullclean")
    p_fullclean.set_defaults(func=cmd_fullclean, ensure=ensure_idf)

    p_flash = sub.add_parser("flash", help="[S3] flash the firmware")
    p_flash.add_argument(
        "-p", "--port",
        help="serial port (default: idf.py autodetect, honors $ESPPORT)",
    )
    p_flash.add_argument(
        "-b", "--baud", type=int, default=460800,
        help="flash baud rate (default: 460800)",
    )
    p_flash.set_defaults(func=cmd_flash, ensure=ensure_idf)

    p_mon = sub.add_parser("monitor", help="[S3] open the serial monitor")
    p_mon.add_argument("-p", "--port", help="serial port (default: idf.py autodetect)")
    p_mon.set_defaults(func=cmd_monitor, ensure=ensure_idf)

    # RP2040 (PlatformIO), nested under `rp2040` (alias `rp`)
    p_rp = sub.add_parser(
        "rp2040", aliases=["rp"],
        help="RP2040 coprocessor via PlatformIO",
    )
    rp_sub = p_rp.add_subparsers(dest="rp_command", required=True)

    rp_build = rp_sub.add_parser("build", help="[RP2040] pio run")
    rp_build.set_defaults(func=cmd_rp_build, ensure=ensure_pio)

    rp_upload = rp_sub.add_parser("upload", help="[RP2040] pio run -t upload")
    rp_upload.add_argument("-p", "--port", help="upload port (default: platformio.ini)")
    rp_upload.set_defaults(func=cmd_rp_upload, ensure=ensure_pio)

    rp_mon = rp_sub.add_parser("monitor", help="[RP2040] pio device monitor")
    rp_mon.add_argument("-p", "--port", help="serial port (default: platformio.ini)")
    rp_mon.set_defaults(func=cmd_rp_monitor, ensure=ensure_pio)

    sub.add_parser("help", help="show this help message and exit")

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    # Bare `./dev` or `./dev help` prints usage instead of erroring out.
    if args.command in (None, "help"):
        parser.print_help()
        return 0
    args.ensure()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
