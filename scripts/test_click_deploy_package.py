#!/usr/bin/env python3
"""Regression checks for the local click-deploy firmware package scaffold."""

from __future__ import annotations

from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parent.parent
DEPLOY = ROOT / "click_deploy"


class ClickDeployPackageTests(unittest.TestCase):
    def test_scaffold_contains_cross_platform_entrypoints(self) -> None:
        expected = [
            "README.md",
            "package.sh",
            "tools/.gitkeep",
            "scripts/macos_linux/install.sh",
            "scripts/macos_linux/flash_all.sh",
            "scripts/macos_linux/flash_esp32s3.sh",
            "scripts/macos_linux/flash_rp2040.sh",
            "scripts/windows/flash_all.ps1",
            "scripts/windows/flash_esp32s3.ps1",
            "scripts/windows/flash_rp2040.ps1",
        ]
        missing = [path for path in expected if not (DEPLOY / path).exists()]

        self.assertEqual([], missing)

    def test_scaffold_does_not_commit_firmware_or_tool_binaries(self) -> None:
        """Firmware and tool binaries may be populated locally, but they must
        never be committed."""
        forbidden_suffixes = {
            ".bin",
            ".elf",
            ".uf2",
            ".exe",
            ".dll",
            ".dylib",
        }
        tracked = subprocess.check_output(
            ["git", "ls-files", "click_deploy/"], cwd=ROOT, text=True
        ).splitlines()
        offenders = [
            path for path in tracked
            if Path(path).suffix.lower() in forbidden_suffixes
        ]

        self.assertEqual([], offenders)

    def test_package_script_knows_build_outputs(self) -> None:
        text = (DEPLOY / "package.sh").read_text()

        self.assertIn("build/bootloader/bootloader.bin", text)
        self.assertIn("build/partition_table/partition-table.bin", text)
        self.assertIn("build/indicator_ha.bin", text)
        self.assertIn("build/flasher_args.json", text)
        self.assertIn("rp2040/.pio/build/indicator_rp2040/firmware.uf2", text)
        self.assertIn("rp2040/.pio/build/indicator_rp2040/firmware.elf", text)

    def test_package_script_knows_all_targets(self) -> None:
        text = (DEPLOY / "package.sh").read_text()

        for target in ("macos-arm64", "linux-amd64", "linux-aarch64", "windows-amd64"):
            self.assertIn(target, text)
        self.assertNotIn("macos-amd64", text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
