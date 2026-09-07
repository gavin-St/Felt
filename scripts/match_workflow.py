#!/usr/bin/env python3
"""Run, replace, finalize, and publish Felt match results safely."""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import re
import shutil
import sqlite3
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence


SCRIPT_DIRECTORY = Path(__file__).resolve().parent
REPOSITORY = SCRIPT_DIRECTORY.parent
sys.path.insert(0, str(SCRIPT_DIRECTORY))
sys.path.insert(0, str(REPOSITORY / "web" / "scripts"))

import export_dashboard  # noqa: E402
import finalize_match  # noqa: E402
import rebuild_ratings  # noqa: E402
import rebuild_stats  # noqa: E402


DEFAULT_HANDS = 20_000
DEFAULT_STACK = 20_000
DEFAULT_SMALL_BLIND = 50
DEFAULT_BIG_BLIND = 100
DEFAULT_DECISION_CAP_MS = 2
HAND_LOG_NAMES = ("hands.jsonl", "hands.jsonl.gz", "stats.json")


@dataclass(frozen=True)
class Rules:
    hands: int
    seed: int
    stack: int
    small_blind: int
    big_blind: int
    decision_cap_us: int
    duplicate: bool
    equity_adjustment: bool


@dataclass(frozen=True)
class MatchPlan:
    match_id: int | None
    output_name: str
    bot_names: tuple[str, str]
    rules: Rules


@dataclass(frozen=True)
class PendingPublication:
    future: concurrent.futures.Future[int]
    staging: Path
    plan: MatchPlan


def run(command: Sequence[object], cwd: Path = REPOSITORY) -> None:
    rendered = [str(part) for part in command]
    print("+ " + " ".join(rendered), flush=True)
    subprocess.run(rendered, cwd=cwd, check=True)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def target_name(bot_name: str) -> str:
    return bot_name.replace("-", "_")


def bot_library(bot: str, build_directory: Path) -> Path:
    supplied = Path(bot)
    if supplied.is_file():
        return supplied.resolve()
    candidate = build_directory / "bots" / f"{target_name(bot)}.dylib"
    if not candidate.is_file():
        raise ValueError(
            f"bot {bot!r} was not found at {candidate}; build it or pass a .dylib path"
        )
    return candidate.resolve()


def build_targets(build_directory: Path, names: Iterable[str]) -> None:
    targets = sorted({target_name(name) for name in names})
    if not (build_directory / "CMakeCache.txt").exists():
        run(["cmake", "--preset", "release"])
    run(["cmake", "--build", build_directory, "--target", "run_match", *targets])


def run_command(
    runner: Path,
    libraries: tuple[Path, Path],
    rules: Rules,
    output: Path,
    hard_timeout_ms: int | None = None,
) -> list[str]:
    if rules.decision_cap_us % 1000 != 0:
        raise ValueError(
            f"decision cap {rules.decision_cap_us} us cannot be represented by run_match"
        )
    command = [
        str(runner),
        str(libraries[0]),
        str(libraries[1]),
        "--hands",
        str(rules.hands),
        "--seed",
        str(rules.seed),
        "--stack",
        str(rules.stack),
        "--sb",
        str(rules.small_blind),
        "--bb",
        str(rules.big_blind),
        "--decision-cap-ms",
        str(rules.decision_cap_us // 1000),
        "--out",
        str(output),
    ]
    if not rules.duplicate:
        command.append("--no-duplicate")
    if not rules.equity_adjustment:
        command.append("--no-equity-adjust")
    if hard_timeout_ms is not None:
        command.extend(("--hard-timeout-ms", str(hard_timeout_ms)))
    return command


def connect(database: Path) -> sqlite3.Connection:
    connection = sqlite3.connect(database)
    connection.row_factory = sqlite3.Row
    connection.execute("PRAGMA foreign_keys = ON")
    finalize_match.initialize_database(connection)
    return connection


def load_plans(
    database: Path,
    bot_names: set[str],
    prefixes: tuple[str, ...],
    match_ids: set[int],
) -> list[MatchPlan]:
    connection = connect(database)
    rows = connection.execute(
        """SELECT m.id, m.source_directory, CAST(m.match_seed AS INTEGER) AS seed,
                  m.hand_count, rp.starting_stack, rp.small_blind, rp.big_blind,
                  rp.decision_cap_us, rp.duplicate, rp.equity_adjustment,
                  b0.name AS bot0, b1.name AS bot1
           FROM matches m
           JOIN rule_profiles rp ON rp.id = m.rule_profile_id
           JOIN match_players mp0 ON mp0.match_id = m.id AND mp0.bot_slot = 0
           JOIN bots b0 ON b0.id = mp0.bot_id
           JOIN match_players mp1 ON mp1.match_id = m.id AND mp1.bot_slot = 1
           JOIN bots b1 ON b1.id = mp1.bot_id
           ORDER BY m.id"""
    ).fetchall()
    connection.close()

    def selected(row: sqlite3.Row) -> bool:
        names = (row["bot0"], row["bot1"])
        return (
            row["id"] in match_ids
            or any(name in bot_names for name in names)
            or any(name.startswith(prefix) for name in names for prefix in prefixes)
        )

    plans = [
        MatchPlan(
            match_id=row["id"],
            output_name=Path(row["source_directory"]).name,
            bot_names=(row["bot0"], row["bot1"]),
            rules=Rules(
                hands=row["hand_count"],
                seed=row["seed"],
                stack=row["starting_stack"],
                small_blind=row["small_blind"],
                big_blind=row["big_blind"],
                decision_cap_us=row["decision_cap_us"],
                duplicate=bool(row["duplicate"]),
                equity_adjustment=bool(row["equity_adjustment"]),
            ),
        )
        for row in rows
        if selected(row)
    ]
    missing = match_ids - {plan.match_id for plan in plans}
    if missing:
        raise ValueError(f"match id(s) do not exist: {', '.join(map(str, sorted(missing)))}")
    if not plans:
        raise ValueError("no ledger matches matched the requested selector")
    names = [plan.output_name for plan in plans]
    if len(names) != len(set(names)):
        raise ValueError("selected matches have duplicate result directory names")
    return plans


def identity_conflicts(
    database: Path,
    selected_ids: set[int],
    current_hashes: dict[str, str],
) -> list[str]:
    connection = connect(database)
    conflicts: list[str] = []
    for name, current_hash in sorted(current_hashes.items()):
        rows = connection.execute(
            """SELECT DISTINCT m.id, b.sha256
               FROM matches m
               JOIN match_players mp ON mp.match_id = m.id
               JOIN bots b ON b.id = mp.bot_id
               WHERE b.name = ? ORDER BY m.id""",
            (name,),
        ).fetchall()
        stale_outside_scope = [
            row["id"]
            for row in rows
            if row["sha256"] != current_hash and row["id"] not in selected_ids
        ]
        if stale_outside_scope:
            conflicts.append(
                f"{name} changed, but ledger match(es) {', '.join(map(str, stale_outside_scope))} "
                "are outside this rerun"
            )
    connection.close()
    return conflicts


def validate_staged(plan: MatchPlan, directory: Path, libraries: tuple[Path, Path]) -> None:
    summary = finalize_match.load_summary(directory)
    config = summary["config"]
    expected_config = {
        "hand_count": plan.rules.hands,
        "match_seed": plan.rules.seed,
        "starting_stack": plan.rules.stack,
        "small_blind": plan.rules.small_blind,
        "big_blind": plan.rules.big_blind,
        "decision_cap_us": plan.rules.decision_cap_us,
        "duplicate": plan.rules.duplicate,
        "equity_adjustment": plan.rules.equity_adjustment,
    }
    if config != expected_config:
        raise ValueError(f"{directory}: generated rules differ from the replacement plan")
    actual_names = tuple(bot["name"] for bot in summary["bots"])
    if actual_names != plan.bot_names:
        raise ValueError(
            f"{directory}: expected bots {plan.bot_names}, generated {actual_names}"
        )
    actual_hashes = tuple(bot["sha256"] for bot in summary["bots"])
    expected_hashes = tuple(sha256(path) for path in libraries)
    if actual_hashes != expected_hashes:
        raise ValueError(f"{directory}: summary bot hashes do not match the libraries")
    finalize_match.hand_log_path(directory)


def backup_database(database: Path, backup: Path) -> None:
    source = sqlite3.connect(database)
    destination = sqlite3.connect(backup)
    try:
        source.backup(destination)
    finally:
        destination.close()
        source.close()


def restore_database(backup: Path, database: Path) -> None:
    restored = database.with_name(f".{database.name}.restore")
    for path in (restored, Path(f"{restored}-wal"), Path(f"{restored}-shm")):
        if path.exists():
            path.unlink()
    source = sqlite3.connect(backup)
    destination = sqlite3.connect(restored)
    try:
        source.backup(destination)
    finally:
        destination.close()
        source.close()
    for suffix in ("-wal", "-shm"):
        sidecar = Path(f"{database}{suffix}")
        if sidecar.exists():
            sidecar.unlink()
    os.replace(restored, database)


def isolated_import_match(directory: Path, database: Path) -> int:
    """Import in a child process so a native SQLite signal can be rolled back."""
    command = [
        sys.executable,
        str(SCRIPT_DIRECTORY / "finalize_match.py"),
        "--database",
        str(database),
        "--keep-hand-log",
        str(directory),
    ]
    completed = subprocess.run(
        command,
        cwd=REPOSITORY,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.stdout:
        print(completed.stdout, end="", flush=True)
    if completed.stderr:
        print(completed.stderr, end="", file=sys.stderr, flush=True)
    if completed.returncode != 0:
        raise RuntimeError(
            f"match import failed with exit code {completed.returncode}: {directory}"
        )
    match = re.search(r"finalized match_id=(\d+)", completed.stdout)
    if match is None:
        raise RuntimeError(f"match import did not report an id: {directory}")
    return int(match.group(1))


def delete_matches(database: Path, match_ids: Iterable[int]) -> None:
    connection = connect(database)
    try:
        connection.execute("BEGIN IMMEDIATE")
        connection.executemany(
            "DELETE FROM matches WHERE id = ?", ((match_id,) for match_id in match_ids)
        )
        connection.execute(
            "DELETE FROM bots WHERE NOT EXISTS "
            "(SELECT 1 FROM match_players WHERE match_players.bot_id = bots.id)"
        )
        connection.commit()
    except Exception:
        connection.rollback()
        raise
    finally:
        connection.close()


def verify_database(database: Path) -> None:
    connection = connect(database)
    violation = connection.execute("PRAGMA foreign_key_check").fetchone()
    integrity = connection.execute("PRAGMA integrity_check").fetchone()[0]
    connection.close()
    if violation is not None:
        raise ValueError(f"foreign-key check failed: {tuple(violation)}")
    if integrity != "ok":
        raise ValueError(f"SQLite integrity check failed: {integrity}")


def atomic_dashboard_export(database: Path, dashboard: Path, staging: Path) -> None:
    temporary = staging / "dashboard.json"
    export_dashboard.export(database, temporary)
    dashboard.parent.mkdir(parents=True, exist_ok=True)
    os.replace(temporary, dashboard)


def publish(
    plans: list[MatchPlan],
    staged: Path,
    results: Path,
    database: Path,
    dashboard: Path,
    keep_hand_logs: bool,
    publish_dashboard: bool,
    rebuild_global_ratings: bool = True,
    backup_ledger: bool = True,
) -> list[int]:
    database.parent.mkdir(parents=True, exist_ok=True)
    results.mkdir(parents=True, exist_ok=True)
    database_backup = staged / "ledger.backup.sqlite3"
    old_ids = [plan.match_id for plan in plans if plan.match_id is not None]
    if old_ids and not backup_ledger:
        raise ValueError("replacement publication requires a ledger backup")
    if database.exists() and backup_ledger:
        backup_database(database, database_backup)
    backups = staged / "previous-results"
    failed = staged / "unpublished-results"
    backups.mkdir()
    installed: list[tuple[Path, Path]] = []
    imported: list[int] = []
    try:
        for plan in plans:
            source = staged / "matches" / plan.output_name
            destination = results / plan.output_name
            if destination.exists():
                old = backups / plan.output_name
                shutil.move(destination, old)
                try:
                    shutil.move(source, destination)
                except Exception:
                    shutil.move(old, destination)
                    raise
            else:
                shutil.move(source, destination)
            installed.append((destination, source))
        if old_ids:
            delete_matches(database, old_ids)
        for plan in plans:
            imported.append(
                isolated_import_match(results / plan.output_name, database)
            )
        if rebuild_global_ratings:
            rebuild_ratings.rebuild(database, [])
        verify_database(database)
        if publish_dashboard:
            atomic_dashboard_export(database, dashboard, staged)
    except Exception:
        failed.mkdir(exist_ok=True)
        for destination, source in reversed(installed):
            if destination.exists():
                shutil.move(destination, failed / source.name)
            old = backups / destination.name
            if old.exists():
                shutil.move(old, destination)
        if database_backup.exists():
            restore_database(database_backup, database)
        elif not backup_ledger and database.exists():
            connection = connect(database)
            try:
                keys = [
                    finalize_match.match_key(
                        finalize_match.load_summary(failed / plan.output_name)
                    )
                    for plan in plans
                    if (failed / plan.output_name / "summary.json").is_file()
                ]
                cleanup_ids = [
                    row[0]
                    for key in keys
                    for row in connection.execute(
                        "SELECT id FROM matches WHERE match_key = ?", (key,)
                    ).fetchall()
                ]
            finally:
                connection.close()
            if cleanup_ids:
                delete_matches(database, cleanup_ids)
                verify_database(database)
        elif database.exists():
            database.unlink()
        raise
    if not keep_hand_logs:
        for plan in plans:
            for name in HAND_LOG_NAMES:
                path = results / plan.output_name / name
                try:
                    if path.exists():
                        path.unlink()
                except OSError as error:
                    print(f"warning: could not remove {path}: {error}", file=sys.stderr)
    return imported


def publish_queued_match(
    plan: MatchPlan,
    staged: Path,
    results: Path,
    database: Path,
    dashboard: Path,
    keep_hand_logs: bool,
    failure_marker: Path,
) -> int:
    """Publish one batch item in the batch's single background process."""
    if failure_marker.exists():
        raise RuntimeError("an earlier queued publication failed")
    try:
        imported = publish(
            [plan],
            staged,
            results,
            database,
            dashboard,
            keep_hand_logs,
            False,
            False,
            False,
        )
        return imported[0]
    except BaseException:
        failure_marker.touch()
        raise


def finish_queued_publication(
    database: Path,
    dashboard: Path,
    publish_dashboard: bool,
) -> int:
    """Refresh global views once after the queued match imports are complete."""
    ratings = rebuild_ratings.rebuild(database, [])
    verify_database(database)
    if publish_dashboard:
        staging = Path(tempfile.mkdtemp(prefix="felt-batch-dashboard-"))
        try:
            atomic_dashboard_export(database, dashboard, staging)
        finally:
            shutil.rmtree(staging)
    return len(ratings)


def stage_matches(
    plans: list[MatchPlan],
    libraries: dict[str, Path],
    runner: Path,
    staging: Path,
) -> None:
    match_root = staging / "matches"
    match_root.mkdir()
    for index, plan in enumerate(plans, start=1):
        output = match_root / plan.output_name
        pair = (libraries[plan.bot_names[0]], libraries[plan.bot_names[1]])
        print(
            f"[{index}/{len(plans)}] {plan.bot_names[0]} vs {plan.bot_names[1]} "
            f"({plan.rules.hands} hands, seed {plan.rules.seed})",
            flush=True,
        )
        run(run_command(runner, pair, plan.rules, output))
        validate_staged(plan, output, pair)


def refresh(database: Path, dashboard: Path, publish_dashboard: bool) -> None:
    rebuilt = rebuild_stats.rebuild(database, [])
    ratings = rebuild_ratings.rebuild(database, [])
    verify_database(database)
    if publish_dashboard:
        staging = Path(tempfile.mkdtemp(prefix="felt-refresh-"))
        try:
            atomic_dashboard_export(database, dashboard, staging)
        finally:
            shutil.rmtree(staging)
    print(f"refreshed {len(rebuilt)} matches and {len(ratings)} ratings")


def next_output_name(
    results: Path,
    left: str,
    right: str,
    reserved: set[str] | None = None,
) -> str:
    stem = f"{left}-vs-{right}"
    index = 1
    reserved = reserved or set()
    while (
        (results / f"{stem}-{index:03d}").exists()
        or f"{stem}-{index:03d}" in reserved
    ):
        index += 1
    return f"{stem}-{index:03d}"


def safe_output_name(name: str) -> str:
    if not name or name in {".", ".."} or Path(name).name != name:
        raise ValueError("output name must be one directory name, without slashes")
    return name


def existing_pair(database: Path, names: tuple[str, str], rules: Rules) -> int | None:
    if not database.exists():
        return None
    connection = connect(database)
    row = connection.execute(
        """SELECT m.id FROM matches m
           JOIN rule_profiles rp ON rp.id = m.rule_profile_id
           JOIN match_players p0 ON p0.match_id = m.id AND p0.bot_slot = 0
           JOIN bots b0 ON b0.id = p0.bot_id
           JOIN match_players p1 ON p1.match_id = m.id AND p1.bot_slot = 1
           JOIN bots b1 ON b1.id = p1.bot_id
           WHERE ((b0.name = ? AND b1.name = ?) OR (b0.name = ? AND b1.name = ?))
             AND rp.starting_stack = ? AND rp.small_blind = ? AND rp.big_blind = ?
             AND rp.decision_cap_us = ? AND rp.duplicate = ?
             AND rp.equity_adjustment = ? LIMIT 1""",
        (
            names[0], names[1], names[1], names[0], rules.stack,
            rules.small_blind, rules.big_blind, rules.decision_cap_us,
            int(rules.duplicate), int(rules.equity_adjustment),
        ),
    ).fetchone()
    connection.close()
    return row["id"] if row else None


def common_paths(arguments: argparse.Namespace) -> tuple[Path, Path, Path, Path, Path]:
    return (
        arguments.database.resolve(),
        arguments.results_dir.resolve(),
        arguments.build_dir.resolve(),
        arguments.runner.resolve(),
        arguments.dashboard.resolve(),
    )


def rules_from_arguments(arguments: argparse.Namespace, seed: int) -> Rules:
    rules = Rules(
        hands=arguments.hands,
        seed=seed,
        stack=arguments.stack,
        small_blind=arguments.small_blind,
        big_blind=arguments.big_blind,
        decision_cap_us=arguments.decision_cap_ms * 1000,
        duplicate=not arguments.no_duplicate,
        equity_adjustment=not arguments.no_equity_adjustment,
    )
    if rules.duplicate and rules.hands % 2:
        raise ValueError("duplicate matches require an even hand count")
    return rules


def command_play(arguments: argparse.Namespace) -> None:
    database, results, build_directory, runner, dashboard = common_paths(arguments)
    named = [bot for bot in arguments.bots if not Path(bot).is_file()]
    if not arguments.skip_build:
        build_targets(build_directory, named)
    libraries = tuple(bot_library(bot, build_directory) for bot in arguments.bots)
    if not runner.is_file():
        raise ValueError(f"match runner not found at {runner}")
    rules = rules_from_arguments(arguments, arguments.seed)
    requested_names = tuple(arguments.bots)
    if all(not Path(bot).is_file() for bot in requested_names):
        duplicate_id = existing_pair(database, requested_names, rules)
        if duplicate_id is not None:
            raise ValueError(
                f"this pairing already exists as match {duplicate_id}; "
                f"use rerun --match-id {duplicate_id}"
            )
    labels = tuple(
        Path(bot).stem.replace("_", "-") if Path(bot).is_file() else bot
        for bot in requested_names
    )
    output_name = safe_output_name(
        arguments.output_name or next_output_name(results, *labels)
    )
    if (results / output_name).exists():
        raise ValueError(f"result directory already exists: {results / output_name}")
    if database.exists():
        requested_hashes = {
            name: sha256(library)
            for name, library in zip(requested_names, libraries)
            if not Path(name).is_file()
        }
        conflicts = identity_conflicts(database, set(), requested_hashes)
        if conflicts:
            raise ValueError(
                "refusing to mix bot versions:\n  " + "\n  ".join(conflicts)
                + "\nrerun every ledger match involving each changed bot first"
            )
    staging = Path(tempfile.mkdtemp(prefix="felt-play-"))
    try:
        output = staging / "matches" / output_name
        output.parent.mkdir()
        run(
            run_command(
                runner, libraries, rules, output, arguments.hard_timeout_ms
            )
        )
        generated = finalize_match.load_summary(output)
        actual_names = tuple(bot["name"] for bot in generated["bots"])
        if database.exists():
            conflicts = identity_conflicts(
                database,
                set(),
                {name: sha256(library) for name, library in zip(actual_names, libraries)},
            )
            if conflicts:
                raise ValueError(
                    "refusing to mix bot versions:\n  " + "\n  ".join(conflicts)
                    + "\nrerun every ledger match involving each changed bot first"
                )
        duplicate_id = existing_pair(database, actual_names, rules)
        if duplicate_id is not None:
            raise ValueError(
                f"this pairing already exists as match {duplicate_id}; "
                f"use rerun --match-id {duplicate_id}"
            )
        plan = MatchPlan(None, output_name, actual_names, rules)
        validate_staged(plan, output, libraries)
        imported = publish(
            [plan], staging, results, database, dashboard,
            arguments.keep_hand_logs, not arguments.no_dashboard,
        )
        print(f"published match_id={imported[0]} results={results / output_name}")
        shutil.rmtree(staging)
    except Exception:
        print(f"workflow failed; preserved staging at {staging}", file=sys.stderr)
        raise


def batch_match_specs(arguments: argparse.Namespace) -> list[tuple[str, str, int]]:
    specs: list[tuple[str, str, int]] = []
    for left, right, raw_seed in arguments.match:
        try:
            seed = int(raw_seed)
        except ValueError as error:
            raise ValueError(f"match seed must be an integer: {raw_seed!r}") from error
        specs.append((left, right, seed))
    return specs


def reap_publication(pending: PendingPublication) -> int:
    match_id = pending.future.result()
    print(
        f"published match_id={match_id} "
        f"results={pending.plan.output_name}",
        flush=True,
    )
    shutil.rmtree(pending.staging)
    return match_id


def command_batch(arguments: argparse.Namespace) -> None:
    """Run matches serially while one child process publishes completed runs."""
    database, results, build_directory, runner, dashboard = common_paths(arguments)
    if arguments.publish_queue_size < 1:
        raise ValueError("publish queue size must be at least 1")
    specs = batch_match_specs(arguments)
    bot_names = {name for left, right, _ in specs for name in (left, right)}
    if not arguments.skip_build:
        build_targets(build_directory, bot_names)
    if not runner.is_file():
        raise ValueError(f"match runner not found at {runner}")
    libraries = {name: bot_library(name, build_directory) for name in bot_names}
    rules_by_seed = {seed: rules_from_arguments(arguments, seed) for _, _, seed in specs}

    hashes = {name: sha256(path) for name, path in libraries.items()}
    if database.exists():
        conflicts = identity_conflicts(database, set(), hashes)
        if conflicts:
            raise ValueError(
                "refusing to mix bot versions:\n  " + "\n  ".join(conflicts)
                + "\nrerun every ledger match involving each changed bot first"
            )

    seen_pairs: set[tuple[str, str]] = set()
    reserved_names: set[str] = set()
    plans: list[MatchPlan] = []
    for left, right, seed in specs:
        pair_key = tuple(sorted((left, right)))
        if pair_key in seen_pairs:
            raise ValueError(f"batch contains the pairing {left} vs {right} more than once")
        seen_pairs.add(pair_key)
        rules = rules_by_seed[seed]
        duplicate_id = existing_pair(database, (left, right), rules)
        if duplicate_id is not None:
            raise ValueError(
                f"{left} vs {right} already exists as match {duplicate_id}; "
                f"use rerun --match-id {duplicate_id}"
            )
        output_name = next_output_name(results, left, right, reserved_names)
        reserved_names.add(output_name)
        plans.append(MatchPlan(None, output_name, (left, right), rules))

    batch_staging = Path(tempfile.mkdtemp(prefix="felt-batch-"))
    failure_marker = batch_staging / "publication-failed"
    pending: list[PendingPublication] = []
    imported: list[int] = []
    executor = concurrent.futures.ProcessPoolExecutor(max_workers=1)
    try:
        for index, plan in enumerate(plans, start=1):
            while pending and pending[0].future.done():
                imported.append(reap_publication(pending.pop(0)))
            while len(pending) >= arguments.publish_queue_size:
                imported.append(reap_publication(pending.pop(0)))

            staging = batch_staging / f"job-{index:04d}"
            output = staging / "matches" / plan.output_name
            output.parent.mkdir(parents=True)
            pair = (
                libraries[plan.bot_names[0]],
                libraries[plan.bot_names[1]],
            )
            print(
                f"[{index}/{len(plans)}] {plan.bot_names[0]} vs "
                f"{plan.bot_names[1]} ({plan.rules.hands} hands, "
                f"seed {plan.rules.seed})",
                flush=True,
            )
            run(
                run_command(
                    runner,
                    pair,
                    plan.rules,
                    output,
                    arguments.hard_timeout_ms,
                )
            )
            validate_staged(plan, output, pair)
            future = executor.submit(
                publish_queued_match,
                plan,
                staging,
                results,
                database,
                dashboard,
                arguments.keep_hand_logs,
                failure_marker,
            )
            pending.append(PendingPublication(future, staging, plan))

        while pending:
            imported.append(reap_publication(pending.pop(0)))
        ratings = executor.submit(
            finish_queued_publication,
            database,
            dashboard,
            not arguments.no_dashboard,
        ).result()
        executor.shutdown()
        shutil.rmtree(batch_staging)
        print(
            f"batch complete: published {len(imported)} matches and "
            f"refreshed {ratings} ratings",
            flush=True,
        )
    except Exception:
        executor.shutdown(wait=True, cancel_futures=True)
        for item in pending:
            if item.future.cancelled():
                continue
            try:
                imported.append(reap_publication(item))
            except Exception:
                pass
        if imported:
            try:
                finish_queued_publication(
                    database, dashboard, not arguments.no_dashboard
                )
            except Exception as refresh_error:
                print(
                    f"warning: could not refresh ratings/dashboard after "
                    f"batch failure: {refresh_error}",
                    file=sys.stderr,
                )
        print(
            f"batch failed after {len(imported)} confirmed publication(s); "
            f"preserved staging at {batch_staging}",
            file=sys.stderr,
        )
        raise


def command_rerun(arguments: argparse.Namespace) -> None:
    database, results, build_directory, runner, dashboard = common_paths(arguments)
    if not database.is_file():
        raise ValueError(f"ledger not found at {database}")
    plans = load_plans(
        database, set(arguments.bot), tuple(arguments.prefix), set(arguments.match_id)
    )
    selected_names = {
        name for plan in plans for name in plan.bot_names
    }
    requested_names = set(arguments.bot)
    requested_names.update(
        name for name in selected_names if any(name.startswith(prefix) for prefix in arguments.prefix)
    )
    if arguments.match_id:
        requested_names.update(selected_names)
    print(f"selected {len(plans)} match(es):")
    for plan in plans:
        print(
            f"  id={plan.match_id} {plan.bot_names[0]} vs {plan.bot_names[1]} "
            f"seed={plan.rules.seed} -> {plan.output_name}"
        )
    if arguments.dry_run:
        return
    if not arguments.skip_build:
        build_targets(build_directory, requested_names)
    if not runner.is_file():
        raise ValueError(f"match runner not found at {runner}")
    libraries = {name: bot_library(name, build_directory) for name in selected_names}
    hashes = {name: sha256(path) for name, path in libraries.items()}
    conflicts = identity_conflicts(
        database, {plan.match_id for plan in plans if plan.match_id is not None}, hashes
    )
    if conflicts:
        raise ValueError(
            "refusing to mix bot versions:\n  " + "\n  ".join(conflicts)
            + "\nrerun every ledger match involving each changed bot"
        )
    staging = Path(tempfile.mkdtemp(prefix="felt-rerun-"))
    try:
        stage_matches(plans, libraries, runner, staging)
        imported = publish(
            plans, staging, results, database, dashboard,
            arguments.keep_hand_logs, not arguments.no_dashboard,
        )
        print(f"replaced {len(plans)} match(es); new ids={','.join(map(str, imported))}")
        shutil.rmtree(staging)
    except Exception:
        print(f"workflow failed; preserved staging at {staging}", file=sys.stderr)
        raise


def add_paths(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--database", type=Path, default=REPOSITORY / "data" / "felt.sqlite3"
    )
    parser.add_argument(
        "--results-dir", type=Path, default=REPOSITORY / "results"
    )
    parser.add_argument(
        "--build-dir", type=Path, default=REPOSITORY / "build" / "release"
    )
    parser.add_argument(
        "--runner",
        type=Path,
        default=REPOSITORY / "build" / "release" / "harness" / "run_match",
    )
    parser.add_argument(
        "--dashboard",
        type=Path,
        default=REPOSITORY / "web" / "data" / "dashboard.json",
    )
    parser.add_argument(
        "--no-dashboard", action="store_true", help="do not refresh the web snapshot"
    )


def add_match_rules(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--hands", type=int, default=DEFAULT_HANDS)
    parser.add_argument("--stack", type=int, default=DEFAULT_STACK)
    parser.add_argument("--sb", dest="small_blind", type=int, default=DEFAULT_SMALL_BLIND)
    parser.add_argument("--bb", dest="big_blind", type=int, default=DEFAULT_BIG_BLIND)
    parser.add_argument("--decision-cap-ms", type=int, default=DEFAULT_DECISION_CAP_MS)
    parser.add_argument(
        "--hard-timeout-ms",
        type=int,
        help="wall timeout per decision (default: derived by run_match)",
    )
    parser.add_argument("--no-duplicate", action="store_true")
    parser.add_argument("--no-equity-adjustment", action="store_true")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--keep-hand-logs", action="store_true")


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser(description=__doc__)
    commands = root.add_subparsers(dest="command", required=True)

    play = commands.add_parser("play", help="run and publish one new matchup")
    play.add_argument("bots", nargs=2, metavar="BOT")
    play.add_argument("--seed", type=int, required=True)
    add_match_rules(play)
    play.add_argument("--output-name")
    add_paths(play)
    play.set_defaults(handler=command_play)

    batch = commands.add_parser(
        "batch",
        help="run new matches serially and publish them through a background queue",
    )
    batch.add_argument(
        "--match",
        action="append",
        nargs=3,
        required=True,
        metavar=("BOT_A", "BOT_B", "SEED"),
        help="queue one new matchup; repeat for additional matchups",
    )
    batch.add_argument(
        "--publish-queue-size",
        type=int,
        default=2,
        help="maximum completed matches awaiting publication (default: 2)",
    )
    add_match_rules(batch)
    add_paths(batch)
    batch.set_defaults(handler=command_batch)

    rerun = commands.add_parser(
        "rerun", help="replace existing ledger matches while preserving their settings"
    )
    rerun.add_argument("--bot", action="append", default=[], help="rerun every match containing this bot")
    rerun.add_argument("--prefix", action="append", default=[], help="rerun existing matches containing a bot with this prefix")
    rerun.add_argument("--match-id", action="append", type=int, default=[], help="rerun this exact ledger match")
    rerun.add_argument("--dry-run", action="store_true")
    rerun.add_argument("--skip-build", action="store_true")
    rerun.add_argument("--keep-hand-logs", action="store_true")
    add_paths(rerun)
    rerun.set_defaults(handler=command_rerun)

    refresh_command = commands.add_parser(
        "refresh", help="rebuild all derived stats, ratings, and dashboard data"
    )
    add_paths(refresh_command)
    refresh_command.set_defaults(
        handler=lambda arguments: refresh(
            arguments.database.resolve(),
            arguments.dashboard.resolve(),
            not arguments.no_dashboard,
        )
    )
    return root


def main() -> int:
    arguments = parser().parse_args()
    if arguments.command == "rerun" and not (
        arguments.bot or arguments.prefix or arguments.match_id
    ):
        parser().error("rerun requires --bot, --prefix, or --match-id")
    try:
        arguments.handler(arguments)
    except (
        OSError,
        ValueError,
        KeyError,
        json.JSONDecodeError,
        sqlite3.Error,
        subprocess.CalledProcessError,
        RuntimeError,
    ) as error:
        print(f"match_workflow: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
