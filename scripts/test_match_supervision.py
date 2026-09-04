#!/usr/bin/env python3

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import finalize_match  # noqa: E402


def main() -> int:
    if len(sys.argv) != 5:
        raise SystemExit(
            "usage: test_match_supervision.py RUN_MATCH HANGING_BOT CRASHING_BOT OTHER_BOT"
        )
    run_match, hanging_bot, crashing_bot, other_bot = sys.argv[1:]
    with tempfile.TemporaryDirectory() as temporary:
        output = Path(temporary) / "timeout-match"
        started = time.monotonic()
        completed = subprocess.run(
            [
                run_match,
                other_bot,
                hanging_bot,
                "--hands",
                "2",
                "--seed",
                "17",
                "--hard-timeout-ms",
                "100",
                "--out",
                str(output),
            ],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )
        elapsed = time.monotonic() - started
        if completed.returncode != 124:
            raise AssertionError(
                f"expected timeout exit 124, got {completed.returncode}: "
                f"{completed.stderr}"
            )
        if elapsed >= 3:
            raise AssertionError(f"hung match took too long to abort: {elapsed:.3f}s")
        if "exceeded the 100 ms hard timeout" not in completed.stderr:
            raise AssertionError(f"missing timeout diagnostic: {completed.stderr}")

        summary = json.loads((output / "summary.json").read_text(encoding="utf-8"))
        if summary["status"] != "aborted" or summary["result"] is not None:
            raise AssertionError("timed-out match was not marked aborted")
        abort = summary["abort"]
        decision = abort["active_decision"]
        if (
            abort["reason"] != "decision_wall_timeout"
            or abort["completed_hands"] != 1
            or abort["hard_timeout_ms"] != 100
            or decision["hand_index"] != 1
            or decision["decision_index"] != 1
            or decision["bot_index"] != 1
        ):
            raise AssertionError(f"unexpected abort metadata: {abort}")
        completed_lines = (output / "hands.jsonl").read_text(
            encoding="utf-8"
        ).splitlines()
        if (
            len(completed_lines) != 1
            or json.loads(completed_lines[0])["hand_index"] != 0
        ):
            raise AssertionError("completed hand was not preserved before timeout")
        try:
            finalize_match.load_summary(output)
        except ValueError as error:
            if "not complete" not in str(error):
                raise
        else:
            raise AssertionError("aborted match was accepted for finalization")

        crash_output = Path(temporary) / "crash-match"
        crashed = subprocess.run(
            [
                run_match,
                crashing_bot,
                other_bot,
                "--hands",
                "2",
                "--seed",
                "17",
                "--out",
                str(crash_output),
            ],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )
        if crashed.returncode != 137:
            raise AssertionError(
                f"expected signal exit 137, got {crashed.returncode}: {crashed.stderr}"
            )
        if "worker terminated by signal 9" not in crashed.stderr:
            raise AssertionError(f"missing crash diagnostic: {crashed.stderr}")
        crash_summary = json.loads(
            (crash_output / "summary.json").read_text(encoding="utf-8")
        )
        crash_abort = crash_summary["abort"]
        if (
            crash_summary["status"] != "aborted"
            or crash_summary["result"] is not None
            or crash_abort["reason"] != "worker_signal"
            or crash_abort["active_decision"]["bot_index"] != 0
        ):
            raise AssertionError(f"unexpected crash summary: {crash_summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
