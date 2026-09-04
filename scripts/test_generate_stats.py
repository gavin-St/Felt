#!/usr/bin/env python3

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_stats  # noqa: E402


def decision(position: int, action_type: int, amount_to: int) -> dict:
    return {
        "position": position,
        "street": 0,
        "my_stack": 95 if position == 0 else 90,
        "my_street_contribution": 5 if position == 0 else 10,
        "applied": {"type": action_type, "amount_to": amount_to},
        "cpu_time_ns": 100 + position,
        "wall_time_ns": 200 + position,
        "violation": 0,
    }


def hand(hand_index: int, mapping: list[int]) -> dict:
    return {
        "hand_index": hand_index,
        "bot_by_position": mapping,
        "hole_cards": [[48, 44], [40, 36]],
        "decisions": [decision(0, 4, 100), decision(1, 3, 0)],
        "result": {
            "reason": 2,
            "ending_street": 0,
            "folded_position": None,
            "committed": [100, 100],
            "raw_net": [100, -100],
            "adjusted_net": [0, 0],
            "equity": {"boards": 2, "wins": [1, 1], "ties": 0},
            "showdown_rank": [10, 5],
        },
    }


def summary() -> dict:
    return {
        "schema_version": 2,
        "status": "complete",
        "config": {
            "hand_count": 2,
            "starting_stack": 100,
            "big_blind": 10,
            "duplicate": True,
        },
        "bots": [
            {"index": 0, "name": "a", "sha256": "aa"},
            {"index": 1, "name": "b", "sha256": "bb"},
        ],
        "result": {
            "raw_net_by_bot": [0, 0],
            "adjusted_net_by_bot": [0, 0],
            "raw_net_by_bot_and_position": [[100, -100], [100, -100]],
            "adjusted_net_by_bot_and_position": [[0, 0], [0, 0]],
        },
    }


class GenerateStatsTest(unittest.TestCase):
    def write_fixture(self, directory: Path, data: dict | None = None) -> None:
        (directory / "summary.json").write_text(
            json.dumps(data or summary()), encoding="utf-8"
        )
        with (directory / "hands.jsonl").open("w", encoding="utf-8") as output:
            output.write(json.dumps(hand(0, [0, 1])) + "\n")
            output.write(json.dumps(hand(1, [1, 0])) + "\n")

    def test_generates_and_cross_foots_stats(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.write_fixture(directory)
            output = generate_stats.generate(directory)
            data = json.loads(output.read_text(encoding="utf-8"))
            self.assertFalse((directory / "hands.jsonl").exists())
            self.assertTrue((directory / "hands.jsonl.gz").is_file())
            self.assertEqual(data["source"]["hands_file"], "hands.jsonl.gz")
            self.assertEqual(data["match"]["independent_samples"], 1)
            for bot in data["bots"]:
                self.assertEqual(bot["results"]["raw_net_chips"], 0)
                self.assertEqual(bot["results"]["adjusted_net_chips"], 0)
                self.assertEqual(bot["all_in"]["reached"]["percentage"], 100.0)
                self.assertEqual(bot["all_in"]["initiated"]["percentage"], 50.0)
                self.assertEqual(sum(row["hands"] for row in bot["starting_hands"]), 2)

            regenerated = generate_stats.generate(directory)
            self.assertEqual(regenerated, output)

    def test_rejects_summary_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            bad_summary = summary()
            bad_summary["result"]["raw_net_by_bot"][0] = 1
            self.write_fixture(directory, bad_summary)
            with self.assertRaisesRegex(ValueError, "raw bot total"):
                generate_stats.generate(directory)


if __name__ == "__main__":
    unittest.main()
