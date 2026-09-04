#!/usr/bin/env python3

from __future__ import annotations

import json
import sqlite3
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_match  # noqa: E402
import finalize_match  # noqa: E402
import rebuild_stats  # noqa: E402


def decision(position: int, action_type: int, amount_to: int) -> dict:
    return {
        "position": position,
        "street": 0,
        "my_stack": 95 if position == 0 else 90,
        "my_street_contribution": 5 if position == 0 else 10,
        "requested": {"type": action_type, "amount_to": amount_to},
        "applied": {"type": action_type, "amount_to": amount_to},
        "pot": 15 if position == 0 else 110,
        "to_call": 5 if position == 0 else 90,
        "cpu_time_ns": 100 + position,
        "wall_time_ns": 200 + position,
        "violation": 0,
    }


def hand(hand_index: int, mapping: list[int]) -> dict:
    return {
        "schema_version": 2,
        "hand_index": hand_index,
        "pair_index": 0,
        "deal_index": 0,
        "bot_by_position": mapping,
        "hole_cards": [[48, 44], [40, 36]],
        "board": [0, 1, 2, 3, 4],
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
        "harness_version": "test",
        "status": "complete",
        "config": {
            "hand_count": 2,
            "match_seed": 42,
            "starting_stack": 100,
            "small_blind": 5,
            "big_blind": 10,
            "decision_cap_us": 2000,
            "duplicate": True,
            "equity_adjustment": True,
        },
        "bots": [
            {"index": 0, "name": "a", "path": "a.dylib", "sha256": "aa"},
            {"index": 1, "name": "b", "path": "b.dylib", "sha256": "bb"},
        ],
        "result": {
            "hand_count": 2,
            "raw_net_by_bot": [0, 0],
            "adjusted_net_by_bot": [0, 0],
            "raw_net_by_bot_and_position": [[100, -100], [100, -100]],
            "adjusted_net_by_bot_and_position": [[0, 0], [0, 0]],
        },
    }


class FinalizeMatchTest(unittest.TestCase):
    def write_fixture(self, directory: Path, data: dict | None = None) -> list[str]:
        (directory / "summary.json").write_text(
            json.dumps(data or summary()), encoding="utf-8"
        )
        lines = [json.dumps(hand(0, [0, 1])), json.dumps(hand(1, [1, 0]))]
        (directory / "hands.jsonl").write_text("\n".join(lines) + "\n", encoding="utf-8")
        return lines

    def test_imports_rebuilds_and_exports(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            match_directory = root / "match"
            match_directory.mkdir()
            expected_lines = self.write_fixture(match_directory)
            database = root / "felt.sqlite3"
            match_id, hands = finalize_match.import_match(match_directory, database)
            self.assertEqual(hands, 2)
            self.assertFalse((match_directory / "hands.jsonl").exists())

            connection = sqlite3.connect(database)
            counts = connection.execute(
                "SELECT (SELECT COUNT(*) FROM hands), "
                "(SELECT COUNT(*) FROM hand_players), "
                "(SELECT COUNT(*) FROM actions), "
                "(SELECT COUNT(*) FROM hand_chunks)"
            ).fetchone()
            self.assertEqual(counts, (2, 4, 4, 1))
            stats = connection.execute(
                """SELECT raw_net_chips, adjusted_net_chips,
                          all_in_reached_percentage, all_in_initiated_percentage
                   FROM v_match_bot_stats ORDER BY bot_slot"""
            ).fetchall()
            self.assertEqual(stats, [(0, 0, 100.0, 50.0), (0, 0, 100.0, 50.0)])
            all_ins = connection.execute(
                """SELECT bot_slot, count FROM all_in_stats
                   WHERE kind = 'initiated' AND street = 0 ORDER BY bot_slot"""
            ).fetchall()
            self.assertEqual(all_ins, [(0, 1), (1, 1)])
            payload = connection.execute("SELECT jsonl FROM hand_chunks").fetchone()[0]
            self.assertEqual(zlib.decompress(payload).decode().splitlines(), expected_lines)
            connection.close()

            self.assertEqual(rebuild_stats.rebuild(database, [match_id]), [match_id])
            export_directory = root / "export"
            _, hand_path, exported = export_match.export_match(
                database, match_id, export_directory
            )
            self.assertEqual(exported, 2)
            self.assertEqual(hand_path.read_text(encoding="utf-8").splitlines(), expected_lines)
            self.assertEqual(
                json.loads((export_directory / "summary.json").read_text()), summary()
            )

    def test_duplicate_import_requires_replace(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.write_fixture(root)
            database = root / "felt.sqlite3"
            finalize_match.import_match(root, database, keep_hand_log=True)
            with self.assertRaisesRegex(ValueError, "already exists"):
                finalize_match.import_match(root, database, keep_hand_log=True)
            replacement, _ = finalize_match.import_match(
                root, database, replace=True, keep_hand_log=True
            )
            self.assertGreater(replacement, 0)

    def test_rolls_back_and_keeps_log_on_summary_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bad_summary = summary()
            bad_summary["result"]["raw_net_by_bot"][0] = 1
            self.write_fixture(root, bad_summary)
            database = root / "felt.sqlite3"
            with self.assertRaisesRegex(ValueError, "totals do not match"):
                finalize_match.import_match(root, database)
            self.assertTrue((root / "hands.jsonl").is_file())
            connection = sqlite3.connect(database)
            self.assertEqual(connection.execute("SELECT COUNT(*) FROM matches").fetchone()[0], 0)
            connection.close()


if __name__ == "__main__":
    unittest.main()
