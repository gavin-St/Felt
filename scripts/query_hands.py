#!/usr/bin/env python3
"""Query Felt's per-bot hand index and optionally recover full histories."""

from __future__ import annotations

import argparse
import json
import secrets
import sqlite3
import sys
import zlib
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
from finalize_match import initialize_database  # noqa: E402


STREET_NAMES = {"preflop": 0, "flop": 1, "turn": 2, "river": 3}


def cursor(value: str) -> tuple[int, int, int]:
    try:
        parts = tuple(int(part) for part in value.split(":"))
    except ValueError as error:
        raise argparse.ArgumentTypeError("cursor must be MATCH:HAND:BOT_SLOT") from error
    if len(parts) != 3 or any(part < 0 for part in parts):
        raise argparse.ArgumentTypeError("cursor must be MATCH:HAND:BOT_SLOT")
    return parts


def add_filter(
    clauses: list[str], parameters: list[Any], expression: str, value: Any
) -> None:
    if value is not None:
        clauses.append(expression + " = ?")
        parameters.append(value)


def filters(arguments: argparse.Namespace) -> tuple[list[str], list[Any]]:
    clauses: list[str] = []
    parameters: list[Any] = []
    for expression, value in (
        ("hp.bot_id", arguments.bot),
        ("hp.opponent_bot_id", arguments.opponent),
        ("hp.match_id", arguments.match),
        ("hp.bucket", arguments.bucket),
        ("hp.exact_combo", arguments.combo),
        ("hp.position", arguments.position),
        ("hp.pot_class", arguments.pot_class),
        ("hp.outcome", arguments.outcome),
    ):
        add_filter(clauses, parameters, expression, value)
    for expression, value in (
        ("hp.saw_flop", arguments.saw_flop),
        ("hp.showdown", arguments.showdown),
        ("hp.all_in_initiated", arguments.all_in_initiated),
    ):
        if value is not None:
            add_filter(clauses, parameters, expression, int(value == "yes"))
    for expression, value in (
        ("hp.all_in_street", arguments.all_in_street),
        ("hp.all_in_initiated_street", arguments.all_in_initiated_street),
    ):
        if value == "none":
            clauses.append(expression + " IS NULL")
        elif value is not None:
            add_filter(clauses, parameters, expression, STREET_NAMES[value])
    if arguments.adjusted_outcome == "win":
        clauses.append("hp.adjusted_net_chips > 0")
    elif arguments.adjusted_outcome == "loss":
        clauses.append("hp.adjusted_net_chips < 0")
    elif arguments.adjusted_outcome == "chop":
        clauses.append("hp.adjusted_net_chips = 0")
    if arguments.min_pot_bb is not None:
        clauses.append("1.0 * hp.final_pot_chips / rp.big_blind >= ?")
        parameters.append(arguments.min_pot_bb)
    if arguments.max_pot_bb is not None:
        clauses.append("1.0 * hp.final_pot_chips / rp.big_blind <= ?")
        parameters.append(arguments.max_pot_bb)
    return clauses, parameters


def history(connection: sqlite3.Connection, match_id: int, hand_index: int) -> Any:
    row = connection.execute(
        """SELECT c.jsonl, h.history_line_index
           FROM hands h JOIN hand_chunks c
             ON c.match_id = h.match_id
            AND c.chunk_index = h.history_chunk_index
           WHERE h.match_id = ? AND h.hand_index = ?""",
        (match_id, hand_index),
    ).fetchone()
    if row is None:
        raise ValueError(f"hand {match_id}:{hand_index} does not exist")
    lines = zlib.decompress(row[0]).decode("utf-8").splitlines()
    return json.loads(lines[row[1]])


def query(connection: sqlite3.Connection, arguments: argparse.Namespace) -> list[dict[str, Any]]:
    clauses, parameters = filters(arguments)
    order = "hp.match_id, hp.hand_index, hp.bot_slot"
    if arguments.after is not None:
        clauses.append("(hp.match_id, hp.hand_index, hp.bot_slot) > (?, ?, ?)")
        parameters.extend(arguments.after)
    if arguments.neighbor is not None:
        operator = ">" if arguments.direction == "next" else "<"
        clauses.append(
            f"(hp.match_id, hp.hand_index, hp.bot_slot) {operator} (?, ?, ?)"
        )
        parameters.extend(arguments.neighbor)
        order += " ASC" if arguments.direction == "next" else " DESC"
    elif arguments.random:
        pivot = secrets.randbits(63)
        base_clauses = clauses[:]
        base_parameters = parameters[:]
        clauses.append("hp.random_key >= ?")
        parameters.append(pivot)
        order = "hp.random_key"
    else:
        order += " ASC"

    where = " AND ".join(clauses) if clauses else "1 = 1"
    sql = f"""SELECT hp.match_id, hp.hand_index, hp.bot_slot,
                     hp.bot_id, b.name, b.sha256,
                     hp.opponent_bot_id, o.name, o.sha256,
                     hp.position, hp.bucket, hp.exact_combo, hp.pot_class,
                     hp.saw_flop, hp.showdown, hp.outcome,
                     hp.raw_net_chips, hp.adjusted_net_chips,
                     hp.all_in_street, hp.all_in_initiated,
                     hp.all_in_initiated_street,
                     1.0 * hp.final_pot_chips / rp.big_blind AS final_pot_bb
              FROM hand_players hp
              JOIN bots b ON b.id = hp.bot_id
              JOIN bots o ON o.id = hp.opponent_bot_id
              JOIN matches m ON m.id = hp.match_id
              JOIN rule_profiles rp ON rp.id = m.rule_profile_id
              WHERE {where} ORDER BY {order}"""
    limit = 1 if arguments.random or arguments.neighbor is not None else arguments.limit
    if arguments.random or arguments.neighbor is not None or not arguments.all:
        sql += " LIMIT ?"
        parameters.append(limit)
    rows = connection.execute(sql, parameters).fetchall()
    if arguments.random and not rows:
        # Stable random keys are uniform; wrapping around makes the pivot method
        # select one matching row without sorting the whole result set randomly.
        fallback_where = " AND ".join(base_clauses) if base_clauses else "1 = 1"
        fallback_sql = sql.split(" WHERE ", 1)[0] + (
            f" WHERE {fallback_where} ORDER BY hp.random_key LIMIT 1"
        )
        rows = connection.execute(fallback_sql, base_parameters).fetchall()

    columns = (
        "match_id", "hand_index", "bot_slot", "bot_id", "bot_name", "bot_sha256",
        "opponent_bot_id", "opponent_name", "opponent_sha256", "position", "bucket",
        "exact_combo", "pot_class", "saw_flop", "showdown", "outcome",
        "raw_net_chips", "adjusted_net_chips", "all_in_street", "all_in_initiated",
        "all_in_initiated_street", "final_pot_bb",
    )
    output = [dict(zip(columns, row)) for row in rows]
    if arguments.include_history:
        for record in output:
            record["history"] = history(
                connection, record["match_id"], record["hand_index"]
            )
    return output


def yes_no(value: str) -> str:
    if value not in {"yes", "no"}:
        raise argparse.ArgumentTypeError("expected yes or no")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description="Query indexed Felt hands as JSON lines.")
    parser.add_argument("--database", type=Path, default=Path("data/felt.sqlite3"))
    parser.add_argument("--bot", type=int, help="bot database id")
    parser.add_argument("--opponent", type=int, help="opponent database id")
    parser.add_argument("--match", type=int)
    parser.add_argument("--bucket")
    parser.add_argument("--combo")
    parser.add_argument("--position", choices=(0, 1), type=int)
    parser.add_argument(
        "--pot-class",
        choices=("walk", "limped_unraised", "single_raised", "three_bet", "four_bet_plus"),
    )
    parser.add_argument("--min-pot-bb", type=float)
    parser.add_argument("--max-pot-bb", type=float)
    parser.add_argument("--saw-flop", type=yes_no)
    parser.add_argument("--showdown", type=yes_no)
    parser.add_argument("--outcome", choices=("win", "loss", "chop"))
    parser.add_argument("--adjusted-outcome", choices=("win", "loss", "chop"))
    parser.add_argument(
        "--all-in-street", choices=("none", "preflop", "flop", "turn", "river")
    )
    parser.add_argument("--all-in-initiated", type=yes_no)
    parser.add_argument(
        "--all-in-initiated-street",
        choices=("none", "preflop", "flop", "turn", "river"),
    )
    parser.add_argument("--limit", type=int, default=50)
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--after", type=cursor, help="page after MATCH:HAND:BOT_SLOT")
    parser.add_argument("--neighbor", type=cursor, help="find one neighboring indexed hand")
    parser.add_argument("--direction", choices=("previous", "next"), default="next")
    parser.add_argument("--random", action="store_true")
    parser.add_argument("--include-history", action="store_true")
    arguments = parser.parse_args()
    if arguments.limit <= 0:
        parser.error("--limit must be positive")
    if sum((arguments.after is not None, arguments.neighbor is not None, arguments.random)) > 1:
        parser.error("--after, --neighbor, and --random are mutually exclusive")
    try:
        connection = sqlite3.connect(arguments.database)
        initialize_database(connection)
        for record in query(connection, arguments):
            print(json.dumps(record, sort_keys=True, separators=(",", ":")))
        connection.close()
    except (OSError, ValueError, sqlite3.Error, json.JSONDecodeError) as error:
        parser.exit(1, f"query_hands: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
