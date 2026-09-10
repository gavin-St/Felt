#!/usr/bin/env python3

from __future__ import annotations

import concurrent.futures
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
    def test_queued_publish_defers_full_ledger_verification(self) -> None:
        plan = match_workflow.MatchPlan(
            None,
            "a-vs-b-001",
            ("a", "b"),
            match_workflow.Rules(2, 42, 100, 5, 10, 2000, True, True),
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with mock.patch.object(match_workflow, "publish", return_value=[7]) as publish:
                match_id = match_workflow.publish_queued_match(
                    plan,
                    root / "staging",
                    root / "results",
                    root / "felt.sqlite3",
                    root / "dashboard.json",
                    False,
                    root / "publication-failed",
                )
        self.assertEqual(match_id, 7)
        self.assertFalse(publish.call_args.args[-1])

    def test_batch_parser_accepts_repeated_matches(self) -> None:
        arguments = match_workflow.parser().parse_args(
            [
                "batch",
                "--match", "a", "b", "101",
                "--match", "c", "d", "102",
            ]
        )
        self.assertEqual(
            match_workflow.batch_match_specs(arguments),
            [("a", "b", 101), ("c", "d", 102)],
        )
        self.assertEqual(arguments.publish_queue_size, 2)

    def test_integrity_check_is_opt_in(self) -> None:
        root = match_workflow.parser()
        batch = root.parse_args(["batch", "--match", "a", "b", "101"])
        rerun = root.parse_args(["rerun", "--bot", "a", "--integrity-check"])
        refresh = root.parse_args(["refresh", "--skip-integrity-check"])
        self.assertFalse(batch.integrity_check)
        self.assertTrue(rerun.integrity_check)
        self.assertFalse(refresh.integrity_check)

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
        self.assertEqual(command[command.index("--decision-cap-us") + 1], "3000")
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

    def test_incremental_replacement_uses_no_whole_ledger_backup(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            results = root / "results"
            old_directory = results / "a-vs-b-001"
            old_directory.mkdir(parents=True)
            fixture = fixtures.FinalizeMatchTest()
            fixture.write_fixture(old_directory)
            database = root / "felt.sqlite3"
            old_id, _ = finalize_match.import_match(old_directory, database)

            staging = root / "staging"
            new_directory = staging / "matches" / old_directory.name
            new_directory.mkdir(parents=True)
            replacement = fixtures.summary()
            replacement["config"]["match_seed"] = 99
            fixture.write_fixture(new_directory, replacement)
            plan = match_workflow.MatchPlan(
                old_id,
                old_directory.name,
                ("a", "b"),
                match_workflow.Rules(2, 99, 100, 5, 10, 2000, True, True),
            )

            imported = match_workflow.publish_replacements(
                [plan], staging, results, database, root / "dashboard.json",
                True, False, False,
            )

            self.assertEqual(len(imported), 1)
            self.assertFalse((staging / "ledger.backup.sqlite3").exists())
            connection = sqlite3.connect(database)
            rows = connection.execute(
                "SELECT match_seed FROM matches"
            ).fetchall()
            connection.close()
            self.assertEqual(rows, [("99",)])
            published = json.loads((old_directory / "summary.json").read_text())
            self.assertEqual(published["config"]["match_seed"], 99)

    def test_incremental_import_failure_keeps_old_match_and_result(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            results = root / "results"
            old_directory = results / "a-vs-b-001"
            old_directory.mkdir(parents=True)
            fixture = fixtures.FinalizeMatchTest()
            fixture.write_fixture(old_directory)
            database = root / "felt.sqlite3"
            old_id, _ = finalize_match.import_match(old_directory, database)

            staging = root / "staging"
            new_directory = staging / "matches" / old_directory.name
            new_directory.mkdir(parents=True)
            replacement = fixtures.summary()
            replacement["config"]["match_seed"] = 99
            fixture.write_fixture(new_directory, replacement)
            plan = match_workflow.MatchPlan(
                old_id,
                old_directory.name,
                ("a", "b"),
                match_workflow.Rules(2, 99, 100, 5, 10, 2000, True, True),
            )

            with mock.patch.object(
                match_workflow,
                "isolated_import_match",
                side_effect=RuntimeError("intentional failure"),
            ):
                with self.assertRaisesRegex(RuntimeError, "intentional failure"):
                    match_workflow.publish_replacements(
                        [plan], staging, results, database,
                        root / "dashboard.json", True, False, False,
                    )

            connection = sqlite3.connect(database)
            rows = connection.execute(
                "SELECT id, match_seed FROM matches"
            ).fetchall()
            connection.close()
            self.assertEqual(rows, [(old_id, "42")])
            restored = json.loads((old_directory / "summary.json").read_text())
            self.assertEqual(restored["config"]["match_seed"], 42)
            self.assertTrue((new_directory / "hands.jsonl").is_file())

    def test_failed_new_publish_removes_only_new_match_without_ledger_copy(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            results = root / "results"
            old_directory = results / "a-vs-b-001"
            old_directory.mkdir(parents=True)
            fixtures.FinalizeMatchTest().write_fixture(old_directory)
            database = root / "felt.sqlite3"
            old_id, _ = finalize_match.import_match(old_directory, database)

            staging = root / "staging"
            new_directory = staging / "matches" / "c-vs-d-001"
            new_directory.mkdir(parents=True)
            replacement = fixtures.summary()
            replacement["config"]["match_seed"] = 99
            replacement["bots"][0].update(name="c", sha256="cc")
            replacement["bots"][1].update(name="d", sha256="dd")
            fixtures.FinalizeMatchTest().write_fixture(new_directory, replacement)
            plan = match_workflow.MatchPlan(
                None,
                new_directory.name,
                ("c", "d"),
                match_workflow.Rules(2, 99, 100, 5, 10, 2000, True, True),
            )

            with mock.patch.object(
                match_workflow,
                "verify_database",
                side_effect=[ValueError("intentional failure"), None],
            ):
                with self.assertRaisesRegex(ValueError, "intentional failure"):
                    match_workflow.publish(
                        [plan], staging, results, database, root / "dashboard.json",
                        False, False, False, False,
                    )

            self.assertFalse((staging / "ledger.backup.sqlite3").exists())
            connection = sqlite3.connect(database)
            self.assertEqual(
                connection.execute("SELECT id FROM matches").fetchall(),
                [(old_id,)],
            )
            connection.close()
            self.assertTrue(
                (staging / "unpublished-results" / new_directory.name / "hands.jsonl").is_file()
            )

    def test_match_can_publish_in_worker_then_refresh_global_views(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            staging = root / "staging"
            output_name = "a-vs-b-001"
            match_directory = staging / "matches" / output_name
            match_directory.mkdir(parents=True)
            fixtures.FinalizeMatchTest().write_fixture(match_directory)
            plan = match_workflow.MatchPlan(
                None,
                output_name,
                ("a", "b"),
                match_workflow.Rules(2, 42, 100, 5, 10, 2000, True, True),
            )
            results = root / "results"
            database = root / "felt.sqlite3"
            dashboard = root / "dashboard.json"

            with concurrent.futures.ProcessPoolExecutor(max_workers=1) as executor:
                match_id = executor.submit(
                    match_workflow.publish_queued_match,
                    plan,
                    staging,
                    results,
                    database,
                    dashboard,
                    False,
                    root / "publication-failed",
                ).result()
                ratings = executor.submit(
                    match_workflow.finish_queued_publication,
                    database,
                    dashboard,
                    False,
                ).result()

            self.assertEqual(match_id, 1)
            self.assertEqual(ratings, 2)
            self.assertTrue((results / output_name / "summary.json").is_file())
            self.assertFalse((results / output_name / "hands.jsonl").exists())
            connection = sqlite3.connect(database)
            self.assertEqual(connection.execute("SELECT COUNT(*) FROM matches").fetchone()[0], 1)
            self.assertEqual(connection.execute("SELECT COUNT(*) FROM ratings").fetchone()[0], 2)
            connection.close()


if __name__ == "__main__":
    unittest.main()
