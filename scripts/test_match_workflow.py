#!/usr/bin/env python3

from __future__ import annotations

import json
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import finalize_match  # noqa: E402
import match_workflow  # noqa: E402
import test_finalize_match as fixtures  # noqa: E402


class MatchWorkflowTest(unittest.TestCase):
    def test_run_command_preserves_rule_switches(self) -> None:
        rules = match_workflow.Rules(20, 7, 200, 1, 2, 3000, False, False)
        command = match_workflow.run_command(
            Path("runner"),
            (Path("a.dylib"), Path("b.dylib")),
            rules,
            Path("out"),
            9000,
        )
        self.assertIn("--no-duplicate", command)
        self.assertIn("--no-equity-adjust", command)
        self.assertEqual(command[command.index("--decision-cap-ms") + 1], "3")
        self.assertEqual(command[command.index("--hard-timeout-ms") + 1], "9000")

    def test_changed_bot_requires_complete_rerun_scope(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            database = root / "felt.sqlite3"
            fixture = fixtures.FinalizeMatchTest()
            for index, opponent in enumerate(("b", "c"), start=1):
                directory = root / f"match-{index}"
                directory.mkdir()
                data = fixtures.summary()
                data["config"]["match_seed"] = index
                data["bots"][1]["name"] = opponent
                data["bots"][1]["sha256"] = opponent * 2
                fixture.write_fixture(directory, data)
                finalize_match.import_match(directory, database)
            conflicts = match_workflow.identity_conflicts(database, {1}, {"a": "new"})
            self.assertEqual(len(conflicts), 1)
            self.assertIn("match(es) 2", conflicts[0])
            self.assertEqual(
                match_workflow.identity_conflicts(database, {1, 2}, {"a": "new"}),
                [],
            )

    def test_load_plans_preserves_ledger_configuration(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = root / "a-vs-b-001"
            directory.mkdir()
            fixtures.FinalizeMatchTest().write_fixture(directory)
            database = root / "felt.sqlite3"
            finalize_match.import_match(directory, database)
            plans = match_workflow.load_plans(database, {"a"}, (), set())
            self.assertEqual(len(plans), 1)
            self.assertEqual(plans[0].output_name, "a-vs-b-001")
            self.assertEqual(plans[0].bot_names, ("a", "b"))
            self.assertEqual(plans[0].rules.seed, 42)
            self.assertEqual(plans[0].rules.decision_cap_us, 2000)

    def test_delete_matches_removes_orphan_bot_versions(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = root / "match"
            directory.mkdir()
            fixtures.FinalizeMatchTest().write_fixture(directory)
            database = root / "felt.sqlite3"
            match_id, _ = finalize_match.import_match(directory, database)
            match_workflow.delete_matches(database, [match_id])
            connection = sqlite3.connect(database)
            self.assertEqual(connection.execute("SELECT COUNT(*) FROM matches").fetchone()[0], 0)
            self.assertEqual(connection.execute("SELECT COUNT(*) FROM bots").fetchone()[0], 0)
            connection.close()

    def test_refresh_rebuilds_stats_ratings_and_dashboard(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = root / "match"
            directory.mkdir()
            fixtures.FinalizeMatchTest().write_fixture(directory)
            database = root / "felt.sqlite3"
            finalize_match.import_match(directory, database)
            dashboard = root / "dashboard.json"
            match_workflow.refresh(database, dashboard, True)
            self.assertTrue(dashboard.is_file())
            connection = sqlite3.connect(database)
            self.assertEqual(
                connection.execute("SELECT COUNT(*) FROM match_bot_stats").fetchone()[0],
                2,
            )
            self.assertEqual(
                connection.execute("SELECT COUNT(*) FROM ratings").fetchone()[0], 2
            )
            connection.close()

    def test_output_name_cannot_escape_results_directory(self) -> None:
        with self.assertRaises(ValueError):
            match_workflow.safe_output_name("../outside")

    def test_failed_replacement_restores_ledger_and_result_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            results = root / "results"
            old_directory = results / "a-vs-b-001"
            old_directory.mkdir(parents=True)
            fixtures.FinalizeMatchTest().write_fixture(old_directory)
            database = root / "felt.sqlite3"
            old_id, _ = finalize_match.import_match(old_directory, database)

            staging = root / "staging"
            new_directory = staging / "matches" / old_directory.name
            new_directory.mkdir(parents=True)
            replacement = fixtures.summary()
            replacement["config"]["match_seed"] = 99
            fixtures.FinalizeMatchTest().write_fixture(new_directory, replacement)
            plan = match_workflow.MatchPlan(
                old_id,
                old_directory.name,
                ("a", "b"),
                match_workflow.Rules(2, 99, 100, 5, 10, 2000, True, True),
            )

            with mock.patch.object(
                match_workflow.rebuild_ratings,
                "rebuild",
                side_effect=ValueError("intentional failure"),
            ):
                with self.assertRaisesRegex(ValueError, "intentional failure"):
                    match_workflow.publish(
                        [plan], staging, results, database, root / "dashboard.json",
                        False, False,
                    )

            restored = json.loads((old_directory / "summary.json").read_text())
            self.assertEqual(restored["config"]["match_seed"], 42)
            connection = sqlite3.connect(database)
            self.assertEqual(
                connection.execute("SELECT match_seed FROM matches").fetchone()[0],
                "42",
            )
            connection.close()
            self.assertTrue(
                (staging / "unpublished-results" / old_directory.name / "hands.jsonl").is_file()
            )


if __name__ == "__main__":
    unittest.main()
