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

    payload = {
        "profile": dict(profile),
        "ratings": ratings,
        "matrix": matrix,
        "matches": matches,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(payload, separators=(",", ":")), encoding="utf-8")
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
