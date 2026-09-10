#!/usr/bin/env python3
"""Export the local Felt ledger into a compact static dashboard snapshot."""

from __future__ import annotations

import argparse
import json
import sqlite3
from pathlib import Path
from typing import Any


def rows(connection: sqlite3.Connection, sql: str, values: tuple[Any, ...] = ()) -> list[dict[str, Any]]:
    return [dict(row) for row in connection.execute(sql, values)]


def export(database: Path, output: Path) -> None:
    connection = sqlite3.connect(database.resolve())
    connection.row_factory = sqlite3.Row
    profile = connection.execute(
        """SELECT rp.*, COUNT(m.id) AS match_count, SUM(m.hand_count) AS hand_count
           FROM rule_profiles rp JOIN matches m ON m.rule_profile_id = rp.id
           GROUP BY rp.id ORDER BY match_count DESC, rp.id LIMIT 1"""
    ).fetchone()
    if profile is None:
        raise ValueError("the ledger has no completed matches")
    profile_id = profile["id"]
    ratings = rows(
        connection,
        """SELECT r.bot_id, b.name, b.sha256, r.elo, r.standard_error,
                  r.lower_95, r.upper_95, r.match_count, r.hand_count
           FROM ratings r JOIN bots b ON b.id = r.bot_id
           WHERE r.rule_profile_id = ? ORDER BY r.elo DESC""",
        (profile_id,),
    )
    if not ratings:
        raise ValueError("ratings are missing; run scripts/rebuild_ratings.py first")

    matrix = rows(
        connection,
        """SELECT match_id, match_seed, hand_count, bot_id, bot_name,
                  bot_sha256, opponent_bot_id, opponent_name, opponent_sha256,
                  raw_net_chips, adjusted_net_chips, raw_bb_per_hand,
                  adjusted_bb_per_hand, raw_standard_error,
                  adjusted_standard_error
           FROM v_matrix_match_results WHERE rule_profile_id = ?
           ORDER BY match_id, bot_id""",
        (profile_id,),
    )
    match_ids = sorted({row["match_id"] for row in matrix})
    matches: list[dict[str, Any]] = []
    for match_id in match_ids:
        match = dict(
            connection.execute(
                """SELECT m.id, m.match_seed, m.hand_count, m.harness_version,
                          m.imported_at, rp.starting_stack, rp.small_blind,
                          rp.big_blind, rp.decision_cap_us, rp.duplicate,
                          rp.equity_adjustment
                   FROM matches m JOIN rule_profiles rp ON rp.id = m.rule_profile_id
                   WHERE m.id = ?""",
                (match_id,),
            ).fetchone()
        )
        player_rows = rows(
            connection,
            """SELECT s.*, mp.bot_id
               FROM v_match_bot_stats s JOIN match_players mp
                 ON mp.match_id = s.match_id AND mp.bot_slot = s.bot_slot
               WHERE s.match_id = ? ORDER BY s.bot_slot""",
            (match_id,),
        )
        for player in player_rows:
            slot = player["bot_slot"]
            player["positions"] = rows(
                connection,
                """SELECT position, hands, raw_net_chips, adjusted_net_chips
                   FROM position_stats WHERE match_id = ? AND bot_slot = ?
                   ORDER BY position""",
                (match_id, slot),
            )
            player["all_ins"] = rows(
                connection,
                """SELECT kind, street, count, opportunities FROM all_in_stats
                   WHERE match_id = ? AND bot_slot = ? ORDER BY kind, street""",
                (match_id, slot),
            )
            player["pot_classes"] = rows(
                connection,
                """SELECT pot_class, count, opportunities FROM pot_class_stats
                   WHERE match_id = ? AND bot_slot = ? ORDER BY pot_class""",
                (match_id, slot),
            )
            player["actions"] = rows(
                connection,
                """SELECT street, action_type, count, street_decisions
                   FROM action_stats WHERE match_id = ? AND bot_slot = ?
                   ORDER BY street, action_type""",
                (match_id, slot),
            )
            timing = connection.execute(
                "SELECT * FROM timing_stats WHERE match_id = ? AND bot_slot = ?",
                (match_id, slot),
            ).fetchone()
            player["timing"] = dict(timing) if timing else None
            player["violations_by_code"] = rows(
                connection,
                """SELECT violation, count, decisions FROM violation_stats
                   WHERE match_id = ? AND bot_slot = ? ORDER BY violation""",
                (match_id, slot),
            )
            player["buckets"] = rows(
                connection,
                """SELECT group_key AS bucket, hands, raw_net_chips,
                          adjusted_net_chips, wins, losses, chops, all_in_reached,
                          raw_bb_per_hand, adjusted_bb_per_hand
                   FROM v_hand_group_stats
                   WHERE match_id = ? AND bot_slot = ? AND group_type = 'bucket'
                     AND position = -1 ORDER BY group_key""",
                (match_id, slot),
            )
        match["players"] = player_rows
        matches.append(match)

    bot_totals = rows(
        connection,
        """SELECT * FROM v_bot_totals WHERE rule_profile_id = ?
           ORDER BY bot_id""",
        (profile_id,),
    )

    # The summary goes in one file and the per-match detail in one file each.
    # Detail is 98% of the bytes -- the starting-hand buckets alone are 38 kB
    # per player per match -- and the pages that need it all render on the
    # server. Keeping it out of dashboard.json is what keeps it out of the
    # browser bundle: the scorecard is a client component, so anything
    # dashboard.json holds is shipped to every visitor and, past 25 MiB, is
    # refused outright by the Workers asset limit.
    payload = {
        "profile": dict(profile),
        "ratings": ratings,
        "matrix": matrix,
        "bot_totals": bot_totals,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(payload, separators=(",", ":")), encoding="utf-8")

    # One file per match rather than one file holding all of them. Rerunning a
    # single matchup used to rewrite all thirty megabytes, so git stored the
    # whole snapshot again to record eighty kilobytes of change; now it stores
    # the matches that actually changed. Files are written only when their
    # bytes differ, so a re-export after an unrelated rerun leaves the rest of
    # the directory untouched and out of the commit.
    matches_directory = output.with_name("matches")
    matches_directory.mkdir(parents=True, exist_ok=True)
    written = set()
    for match in matches:
        path = matches_directory / f"{match['id']}.json"
        written.add(path.name)
        text = json.dumps(match, separators=(",", ":"))
        if not path.exists() or path.read_text(encoding="utf-8") != text:
            path.write_text(text, encoding="utf-8")
    # A match dropped from the ledger, or re-imported under a new id, would
    # otherwise linger as a file no page links to but every build bundles.
    for stale in matches_directory.glob("*.json"):
        if stale.name not in written:
            stale.unlink()
    # The single-file snapshot this replaced.
    legacy = output.with_name("matches.json")
    if legacy.exists():
        legacy.unlink()
    connection.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", type=Path, default=Path("../data/felt.sqlite3"))
    parser.add_argument("--output", type=Path, default=Path("data/dashboard.json"))
    arguments = parser.parse_args()
    try:
        export(arguments.database, arguments.output)
    except (OSError, ValueError, sqlite3.Error) as error:
        parser.exit(1, f"export_dashboard: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
