#!/usr/bin/env python3
"""Unit tests for the repo-local ./dev command wrapper."""

from __future__ import annotations

import argparse
import io
import subprocess
import unittest
from contextlib import redirect_stdout
from unittest import mock

import dev


class DevCliTests(unittest.TestCase):
    def test_fullclean_is_a_top_level_command(self) -> None:
        args = dev.build_parser().parse_args(["fullclean"])

        self.assertIs(args.func, dev.cmd_fullclean)
        self.assertIs(args.ensure, dev.ensure_idf)

    def test_flash_recovers_from_idf_python_env_mismatch(self) -> None:
        popen_calls: list[list[str]] = []
        run_calls: list[list[str]] = []
        mismatch = (
            "'/Users/spencer/.espressif/python_env/idf5.4_py3.14_env/bin/python' "
            "is currently active in the environment while the project was configured "
            "with '/Users/spencer/.espressif/python_env/idf5.4_py3.13_env/bin/python'. "
            "Run 'idf.py fullclean' to start again.\n"
        )
        popen_results = [
            FakePopen(["idf.py", "-b", "460800", "flash"], [mismatch], 1),
            FakePopen(["idf.py", "fullclean"], ["cleaned\n"], 0),
        ]

        def fake_popen(cmd: list[str], **kwargs: object) -> "FakePopen":
            popen_calls.append(cmd)
            return popen_results.pop(0)

        def fake_run(cmd: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
            run_calls.append(cmd)
            return subprocess.CompletedProcess(cmd, 0)

        args = argparse.Namespace(port=None, baud=460800)
        with (
            mock.patch("dev.subprocess.Popen", side_effect=fake_popen),
            mock.patch("dev.subprocess.run", side_effect=fake_run),
        ):
            self.assertEqual(dev.cmd_flash(args), 0)

        self.assertEqual(popen_calls, [["idf.py", "-b", "460800", "flash"], ["idf.py", "fullclean"]])
        self.assertEqual(run_calls, [["idf.py", "-b", "460800", "flash"]])

    def test_run_idf_capture_streams_output_before_returning(self) -> None:
        def fake_popen(cmd: list[str], **kwargs: object) -> "FakePopen":
            return FakePopen(cmd, ["line one\n", "line two\n"], 0)

        stdout = io.StringIO()
        with mock.patch("dev.subprocess.Popen", side_effect=fake_popen), redirect_stdout(stdout):
            result = dev.run_idf_capture(["flash"])

        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "line one\nline two\n")
        self.assertIn("line one\nline two\n", stdout.getvalue())


class FakePopen:
    def __init__(self, cmd: list[str], stdout_lines: list[str], returncode: int) -> None:
        self.cmd = cmd
        self.stdout = iter(stdout_lines)
        self._returncode = returncode

    def wait(self) -> int:
        return self._returncode


if __name__ == "__main__":
    unittest.main(verbosity=2)
