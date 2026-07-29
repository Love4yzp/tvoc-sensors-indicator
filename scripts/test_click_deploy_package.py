#!/usr/bin/env python3
"""Regression checks for the local click-deploy firmware package scaffold."""

from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parent.parent
DEPLOY = ROOT / "click_deploy"


class ClickDeployPackageTests(unittest.TestCase):
    def test_scaffold_contains_cross_platform_entrypoints(self) -> None:
        expected = [
            "README.md",
            "firmware/esp32s3/.gitkeep",
            "firmware/rp2040/.gitkeep",
            "tools/.gitkeep",
            "flasher/package.sh",
            "flasher/macos_linux/install.sh",
            "flasher/macos_linux/flash_all.sh",
            "flasher/macos_linux/flash_esp32s3.sh",
            "flasher/macos_linux/flash_rp2040.sh",
            "flasher/windows/flash_all.ps1",
            "flasher/windows/flash_esp32s3.ps1",
            "flasher/windows/flash_rp2040.ps1",
        ]
        missing = [path for path in expected if not (DEPLOY / path).exists()]

        self.assertEqual([], missing)

    def test_scaffold_does_not_commit_firmware_or_tool_binaries(self) -> None:
        forbidden_suffixes = {
            ".bin",
            ".elf",
            ".uf2",
            ".exe",
            ".dll",
            ".dylib",
        }
        offenders = [
            str(path.relative_to(ROOT))
            for path in DEPLOY.rglob("*")
            if path.is_file() and path.suffix.lower() in forbidden_suffixes
        ]

        self.assertEqual([], offenders)

    def test_package_script_knows_build_outputs(self) -> None:
        text = (DEPLOY / "flasher/package.sh").read_text()

        self.assertIn("build/bootloader/bootloader.bin", text)
        self.assertIn("build/partition_table/partition-table.bin", text)
        self.assertIn("build/indicator_ha.bin", text)
        self.assertIn("build/flasher_args.json", text)
        self.assertIn("rp2040/.pio/build/indicator_rp2040/firmware.uf2", text)
        self.assertIn("rp2040/.pio/build/indicator_rp2040/firmware.elf", text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
