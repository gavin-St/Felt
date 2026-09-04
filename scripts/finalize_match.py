#!/usr/bin/env python3
"""Import a completed Felt match into the local SQLite ledger."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import math
import os
import sqlite3
import statistics
import zlib
from pathlib import Path
from typing import Any, Iterable


SCHEMA_VERSION = 3
STATS_VERSION = 1
CHUNK_HANDS = 256
POSITIONS = ("button", "big_blind")
STREETS = range(4)
POT_CLASSES = (
    "walk",
    "limped_unraised",
    "single_raised",
    "three_bet",
    "four_bet_plus",
)
ACTION_TYPES = (1, 2, 3, 4)
RANKS = "23456789TJQKA"
SUITS = "cdsh"


SCHEMA = """
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS schema_meta (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS bots (
  id INTEGER PRIMARY KEY,
  sha256 TEXT NOT NULL UNIQUE,
  name TEXT NOT NULL,
  path TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS rule_profiles (
  id INTEGER PRIMARY KEY,
  starting_stack INTEGER NOT NULL,
  small_blind INTEGER NOT NULL,
  big_blind INTEGER NOT NULL,
  decision_cap_us INTEGER NOT NULL,
  duplicate INTEGER NOT NULL,
  equity_adjustment INTEGER NOT NULL,
  UNIQUE(starting_stack, small_blind, big_blind, decision_cap_us,
         duplicate, equity_adjustment)
);

CREATE TABLE IF NOT EXISTS matches (
  id INTEGER PRIMARY KEY,
  match_key TEXT NOT NULL UNIQUE,
  match_seed TEXT NOT NULL,
  hand_count INTEGER NOT NULL,
  rule_profile_id INTEGER NOT NULL REFERENCES rule_profiles(id),
  summary_schema_version INTEGER NOT NULL,
  harness_version TEXT NOT NULL,
  source_directory TEXT NOT NULL,
  summary_json TEXT NOT NULL,
  imported_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS match_players (
  match_id INTEGER NOT NULL REFERENCES matches(id) ON DELETE CASCADE,
  bot_slot INTEGER NOT NULL,
  bot_id INTEGER NOT NULL REFERENCES bots(id),
  raw_net_chips INTEGER NOT NULL,
  adjusted_net_chips INTEGER NOT NULL,
  raw_button_chips INTEGER NOT NULL,
  raw_big_blind_chips INTEGER NOT NULL,
  adjusted_button_chips INTEGER NOT NULL,
  adjusted_big_blind_chips INTEGER NOT NULL,
  PRIMARY KEY(match_id, bot_slot)
);

CREATE TABLE IF NOT EXISTS hand_chunks (
  match_id INTEGER NOT NULL REFERENCES matches(id) ON DELETE CASCADE,
  chunk_index INTEGER NOT NULL,
  first_hand_index INTEGER NOT NULL,
  hand_count INTEGER NOT NULL,
  codec TEXT NOT NULL,
  jsonl BLOB NOT NULL,
  PRIMARY KEY(match_id, chunk_index)
);

CREATE TABLE IF NOT EXISTS hands (
  match_id INTEGER NOT NULL REFERENCES matches(id) ON DELETE CASCADE,
  hand_index INTEGER NOT NULL,
  pair_index INTEGER,
  deal_index INTEGER NOT NULL,
  button_bot_slot INTEGER NOT NULL,
  big_blind_bot_slot INTEGER NOT NULL,
  button_card_1 INTEGER NOT NULL,
  button_card_2 INTEGER NOT NULL,
  big_blind_card_1 INTEGER NOT NULL,
  big_blind_card_2 INTEGER NOT NULL,
  board_1 INTEGER NOT NULL,
  board_2 INTEGER NOT NULL,
  board_3 INTEGER NOT NULL,
  board_4 INTEGER NOT NULL,
  board_5 INTEGER NOT NULL,
  end_reason INTEGER NOT NULL,
  ending_street INTEGER NOT NULL,
  folded_position INTEGER,
  preflop_raise_count INTEGER NOT NULL,
  pot_class TEXT NOT NULL,
  saw_flop INTEGER NOT NULL,
  showdown INTEGER NOT NULL,
  all_in_street INTEGER,
  all_in_initiator_position INTEGER,
  all_in_initiated_street INTEGER,
  final_pot_chips INTEGER NOT NULL,
  raw_button_chips INTEGER NOT NULL,
  raw_big_blind_chips INTEGER NOT NULL,
  adjusted_button_chips INTEGER NOT NULL,
  adjusted_big_blind_chips INTEGER NOT NULL,
  history_chunk_index INTEGER NOT NULL,
  history_line_index INTEGER NOT NULL,
  random_key INTEGER NOT NULL,
  PRIMARY KEY(match_id, hand_index)
);

CREATE TABLE IF NOT EXISTS hand_players (
  match_id INTEGER NOT NULL,
  hand_index INTEGER NOT NULL,
  bot_slot INTEGER NOT NULL,
  bot_id INTEGER NOT NULL REFERENCES bots(id),
  opponent_bot_id INTEGER NOT NULL REFERENCES bots(id),
  position INTEGER NOT NULL,
  bucket TEXT NOT NULL,
  exact_combo TEXT NOT NULL,
  outcome TEXT NOT NULL,
  raw_net_chips INTEGER NOT NULL,
  adjusted_net_chips INTEGER NOT NULL,
  vpip INTEGER NOT NULL,
  pfr INTEGER NOT NULL,
  saw_flop INTEGER NOT NULL,
  showdown INTEGER NOT NULL,
  showdown_win INTEGER NOT NULL,
  cbet_opportunity INTEGER NOT NULL,
  cbet_made INTEGER NOT NULL,
  all_in_reached INTEGER NOT NULL,
  all_in_initiated INTEGER NOT NULL,
  all_in_street INTEGER,
  all_in_initiated_street INTEGER,
  pot_class TEXT NOT NULL,
  final_pot_chips INTEGER NOT NULL,
  exact_equity REAL,
  random_key INTEGER NOT NULL,
  PRIMARY KEY(match_id, hand_index, bot_slot),
  FOREIGN KEY(match_id, hand_index)
    REFERENCES hands(match_id, hand_index) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS actions (
  match_id INTEGER NOT NULL,
  hand_index INTEGER NOT NULL,
  decision_index INTEGER NOT NULL,
  bot_slot INTEGER NOT NULL,
  position INTEGER NOT NULL,
  street INTEGER NOT NULL,
  requested_type INTEGER NOT NULL,
  applied_type INTEGER NOT NULL,
  amount_to INTEGER NOT NULL,
  violation INTEGER NOT NULL,
  pot_chips INTEGER NOT NULL,
  to_call_chips INTEGER NOT NULL,
  cpu_time_ns INTEGER NOT NULL,
  wall_time_ns INTEGER NOT NULL,
  PRIMARY KEY(match_id, hand_index, decision_index),
  FOREIGN KEY(match_id, hand_index)
    REFERENCES hands(match_id, hand_index) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS match_bot_stats (
  match_id INTEGER NOT NULL REFERENCES matches(id) ON DELETE CASCADE,
  bot_slot INTEGER NOT NULL,
  stats_version INTEGER NOT NULL,
  hands INTEGER NOT NULL,
  raw_net_chips INTEGER NOT NULL,
  adjusted_net_chips INTEGER NOT NULL,
  wins INTEGER NOT NULL,
  losses INTEGER NOT NULL,
  chops INTEGER NOT NULL,
  saw_flop INTEGER NOT NULL,
  showdowns INTEGER NOT NULL,
  showdown_wins INTEGER NOT NULL,
  vpip INTEGER NOT NULL,
  pfr INTEGER NOT NULL,
  cbet_opportunities INTEGER NOT NULL,
  cbets INTEGER NOT NULL,
  all_in_reached INTEGER NOT NULL,
  all_in_initiated INTEGER NOT NULL,
  exact_equity_hands INTEGER NOT NULL,
  average_exact_equity REAL,
  showdown_raw_net_chips INTEGER NOT NULL,
  showdown_adjusted_net_chips INTEGER NOT NULL,
  nonshowdown_raw_net_chips INTEGER NOT NULL,
  nonshowdown_adjusted_net_chips INTEGER NOT NULL,
  PRIMARY KEY(match_id, bot_slot)
);

CREATE TABLE IF NOT EXISTS position_stats (
  match_id INTEGER NOT NULL REFERENCES matches(id) ON DELETE CASCADE,
  bot_slot INTEGER NOT NULL,
  position INTEGER NOT NULL,
  hands INTEGER NOT NULL,
  raw_net_chips INTEGER NOT NULL,
  adjusted_net_chips INTEGER NOT NULL,
  PRIMARY KEY(match_id, bot_slot, position)
);

CREATE TABLE IF NOT EXISTS all_in_stats (
  match_id INTEGER NOT NULL REFERENCES matches(id) ON DELETE CASCADE,
  bot_slot INTEGER NOT NULL,
  kind TEXT NOT NULL,
  street INTEGER NOT NULL,
  count INTEGER NOT NULL,
  opportunities INTEGER NOT NULL,
  PRIMARY KEY(match_id, bot_slot, kind, street)
);

CREATE TABLE IF NOT EXISTS pot_class_stats (
  match_id INTEGER NOT NULL REFERENCES matches(id) ON DELETE CASCADE,
  bot_slot INTEGER NOT NULL,
  pot_class TEXT NOT NULL,
  count INTEGER NOT NULL,
  opportunities INTEGER NOT NULL,
  PRIMARY KEY(match_id, bot_slot, pot_class)
);

CREATE TABLE IF NOT EXISTS action_stats (
  match_id INTEGER NOT NULL REFERENCES matches(id) ON DELETE CASCADE,
  bot_slot INTEGER NOT NULL,
  street INTEGER NOT NULL,
  action_type INTEGER NOT NULL,
  count INTEGER NOT NULL,
  street_decisions INTEGER NOT NULL,
  PRIMARY KEY(match_id, bot_slot, street, action_type)
);

CREATE TABLE IF NOT EXISTS timing_stats (
  match_id INTEGER NOT NULL REFERENCES matches(id) ON DELETE CASCADE,
  bot_slot INTEGER NOT NULL,
  decisions INTEGER NOT NULL,
  mean_cpu_time_ns REAL NOT NULL,
  p99_cpu_time_ns INTEGER NOT NULL,
  max_cpu_time_ns INTEGER NOT NULL,
  mean_wall_time_ns REAL NOT NULL,
  p99_wall_time_ns INTEGER NOT NULL,
  max_wall_time_ns INTEGER NOT NULL,
  violations INTEGER NOT NULL,
  PRIMARY KEY(match_id, bot_slot)
);

CREATE TABLE IF NOT EXISTS violation_stats (
  match_id INTEGER NOT NULL REFERENCES matches(id) ON DELETE CASCADE,
  bot_slot INTEGER NOT NULL,
  violation INTEGER NOT NULL,
  count INTEGER NOT NULL,
  decisions INTEGER NOT NULL,
  PRIMARY KEY(match_id, bot_slot, violation)
);

CREATE TABLE IF NOT EXISTS hand_group_stats (
  match_id INTEGER NOT NULL REFERENCES matches(id) ON DELETE CASCADE,
  bot_slot INTEGER NOT NULL,
  group_type TEXT NOT NULL,
  group_key TEXT NOT NULL,
  position INTEGER NOT NULL,
  hands INTEGER NOT NULL,
  raw_net_chips INTEGER NOT NULL,
  adjusted_net_chips INTEGER NOT NULL,
  wins INTEGER NOT NULL,
  losses INTEGER NOT NULL,
  chops INTEGER NOT NULL,
  all_in_reached INTEGER NOT NULL,
  PRIMARY KEY(match_id, bot_slot, group_type, group_key, position)
);

CREATE TABLE IF NOT EXISTS pair_results (
  match_id INTEGER NOT NULL REFERENCES matches(id) ON DELETE CASCADE,
  sample_index INTEGER NOT NULL,
  bot_slot INTEGER NOT NULL,
  hands INTEGER NOT NULL,
  raw_net_chips INTEGER NOT NULL,
  adjusted_net_chips INTEGER NOT NULL,
  PRIMARY KEY(match_id, sample_index, bot_slot)
);

CREATE TABLE IF NOT EXISTS variance_stats (
  match_id INTEGER NOT NULL REFERENCES matches(id) ON DELETE CASCADE,
  bot_slot INTEGER NOT NULL,
  result_type TEXT NOT NULL,
  independent_samples INTEGER NOT NULL,
  hands_per_sample INTEGER NOT NULL,
  mean_chips_per_sample REAL NOT NULL,
  standard_deviation_chips REAL NOT NULL,
  standard_error_chips REAL NOT NULL,
  PRIMARY KEY(match_id, bot_slot, result_type)
);

CREATE TABLE IF NOT EXISTS ratings (
  rule_profile_id INTEGER NOT NULL REFERENCES rule_profiles(id) ON DELETE CASCADE,
  bot_id INTEGER NOT NULL REFERENCES bots(id) ON DELETE CASCADE,
  rating_version INTEGER NOT NULL,
  component_id INTEGER NOT NULL,
  margin_scale_bb_per_hand REAL NOT NULL,
  elo REAL NOT NULL,
  standard_error REAL NOT NULL,
  lower_95 REAL NOT NULL,
  upper_95 REAL NOT NULL,
  match_count INTEGER NOT NULL,
  hand_count INTEGER NOT NULL,
  calculated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY(rule_profile_id, bot_id)
);

CREATE INDEX IF NOT EXISTS hand_players_filter
  ON hand_players(bot_id, opponent_bot_id, bucket, position, pot_class,
                  all_in_street, all_in_initiated_street, showdown, outcome,
                  random_key);
CREATE INDEX IF NOT EXISTS hand_players_match_filter
  ON hand_players(match_id, bot_slot, bucket, exact_combo, position,
                  pot_class, all_in_street, all_in_initiated_street, outcome,
                  random_key);
CREATE INDEX IF NOT EXISTS actions_bot_street
  ON actions(match_id, bot_slot, street, applied_type);
"""

VIEWS = """
DROP VIEW IF EXISTS v_match_bot_stats;
DROP VIEW IF EXISTS v_hand_group_stats;
DROP VIEW IF EXISTS v_matrix_match_results;

CREATE VIEW v_match_bot_stats AS
SELECT s.*, b.name AS bot_name, b.sha256 AS bot_sha256,
       p.big_blind,
       1.0 * s.raw_net_chips / (p.big_blind * s.hands) AS raw_bb_per_hand,
       1.0 * s.adjusted_net_chips / (p.big_blind * s.hands)
         AS adjusted_bb_per_hand,
       100.0 * s.vpip / s.hands AS vpip_percentage,
       100.0 * s.pfr / s.hands AS pfr_percentage,
       100.0 * s.wins / s.hands AS raw_win_percentage,
       100.0 * s.showdowns / s.hands AS showdown_percentage,
       100.0 * s.showdowns / NULLIF(s.saw_flop, 0) AS wtsd_percentage,
       100.0 * s.showdown_wins / NULLIF(s.showdowns, 0) AS w_sd_percentage,
       100.0 * s.cbets / NULLIF(s.cbet_opportunities, 0) AS cbet_percentage,
       100.0 * s.all_in_reached / s.hands AS all_in_reached_percentage,
       100.0 * s.all_in_initiated / s.hands AS all_in_initiated_percentage
FROM match_bot_stats s
JOIN match_players mp
  ON mp.match_id = s.match_id AND mp.bot_slot = s.bot_slot
JOIN bots b ON b.id = mp.bot_id
JOIN matches m ON m.id = s.match_id
JOIN rule_profiles p ON p.id = m.rule_profile_id;

CREATE VIEW v_hand_group_stats AS
SELECT g.*, p.big_blind,
       1.0 * g.raw_net_chips / (p.big_blind * g.hands) AS raw_bb_per_hand,
       1.0 * g.adjusted_net_chips / (p.big_blind * g.hands)
         AS adjusted_bb_per_hand
FROM hand_group_stats g
JOIN matches m ON m.id = g.match_id
JOIN rule_profiles p ON p.id = m.rule_profile_id;

CREATE VIEW v_matrix_match_results AS
SELECT m.id AS match_id,
       m.rule_profile_id,
       m.match_seed,
       m.hand_count,
       p0.bot_id,
       b0.name AS bot_name,
       b0.sha256 AS bot_sha256,
       p1.bot_id AS opponent_bot_id,
       b1.name AS opponent_name,
       b1.sha256 AS opponent_sha256,
       p0.raw_net_chips,
       p0.adjusted_net_chips,
       1.0 * p0.raw_net_chips / (rp.big_blind * m.hand_count)
         AS raw_bb_per_hand,
       1.0 * p0.adjusted_net_chips / (rp.big_blind * m.hand_count)
         AS adjusted_bb_per_hand,
       1.0 * vr.standard_error_chips
         / (rp.big_blind * vr.hands_per_sample) AS raw_standard_error,
       1.0 * va.standard_error_chips
         / (rp.big_blind * va.hands_per_sample) AS adjusted_standard_error
FROM matches m
JOIN rule_profiles rp ON rp.id = m.rule_profile_id
JOIN match_players p0 ON p0.match_id = m.id AND p0.bot_slot = 0
JOIN match_players p1 ON p1.match_id = m.id AND p1.bot_slot = 1
JOIN bots b0 ON b0.id = p0.bot_id
JOIN bots b1 ON b1.id = p1.bot_id
JOIN variance_stats vr
  ON vr.match_id = m.id AND vr.bot_slot = 0 AND vr.result_type = 'raw'
JOIN variance_stats va
  ON va.match_id = m.id AND va.bot_slot = 0 AND va.result_type = 'adjusted'
UNION ALL
SELECT m.id,
       m.rule_profile_id,
       m.match_seed,
       m.hand_count,
       p1.bot_id,
       b1.name,
       b1.sha256,
       p0.bot_id,
       b0.name,
       b0.sha256,
       p1.raw_net_chips,
       p1.adjusted_net_chips,
       1.0 * p1.raw_net_chips / (rp.big_blind * m.hand_count),
       1.0 * p1.adjusted_net_chips / (rp.big_blind * m.hand_count),
       1.0 * vr.standard_error_chips
         / (rp.big_blind * vr.hands_per_sample),
       1.0 * va.standard_error_chips
         / (rp.big_blind * va.hands_per_sample)
FROM matches m
JOIN rule_profiles rp ON rp.id = m.rule_profile_id
JOIN match_players p0 ON p0.match_id = m.id AND p0.bot_slot = 0
JOIN match_players p1 ON p1.match_id = m.id AND p1.bot_slot = 1
JOIN bots b0 ON b0.id = p0.bot_id
JOIN bots b1 ON b1.id = p1.bot_id
JOIN variance_stats vr
  ON vr.match_id = m.id AND vr.bot_slot = 1 AND vr.result_type = 'raw'
JOIN variance_stats va
  ON va.match_id = m.id AND va.bot_slot = 1 AND va.result_type = 'adjusted';
"""


def integer(value: Any, name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{name} must be an integer")
    return value


def card_text(card: int) -> str:
    if card < 0 or card >= 52:
        raise ValueError(f"invalid card value {card}")
    return RANKS[card >> 2] + SUITS[card & 3]


def ordered_hole(cards: list[int]) -> tuple[int, int]:
    if len(cards) != 2:
        raise ValueError("hole-card pair must contain two cards")
    first, second = cards
    card_text(first)
    card_text(second)
    if (first >> 2, first) < (second >> 2, second):
        first, second = second, first
    return first, second


def bucket_label(cards: list[int]) -> str:
    first, second = ordered_hole(cards)
    high = RANKS[first >> 2]
    low = RANKS[second >> 2]
    if high == low:
        return high + low
    return high + low + ("s" if (first & 3) == (second & 3) else "o")


def combo_label(cards: list[int]) -> str:
    first, second = ordered_hole(cards)
    return card_text(first) + card_text(second)


def hand_log_path(directory: Path) -> Path:
    for name in ("hands.jsonl", "hands.jsonl.gz"):
        path = directory / name
        if path.is_file():
            return path
    raise ValueError(f"{directory}: no hands.jsonl or hands.jsonl.gz")


def open_hand_log(path: Path):
    if path.suffix == ".gz":
        return gzip.open(path, "rt", encoding="utf-8")
    return path.open(encoding="utf-8")


def raw_outcomes(result: dict[str, Any]) -> list[str]:
    if result["reason"] == 1:
        folded = integer(result["folded_position"], "folded_position")
        winner = 1 - folded
    elif result["reason"] == 2:
        ranks = result["showdown_rank"]
        if ranks[0] == ranks[1]:
            return ["chop", "chop"]
        winner = 0 if ranks[0] > ranks[1] else 1
    else:
        raise ValueError("invalid hand end reason")
    return ["win" if position == winner else "loss" for position in range(2)]


def hand_features(
    hand: dict[str, Any], starting_stack: int
) -> dict[str, Any]:
    result = hand["result"]
    showdown = result["reason"] == 2
    saw_flop = showdown or result["ending_street"] >= 1
    raises = [
        decision
        for decision in hand["decisions"]
        if decision["street"] == 0 and decision["applied"]["type"] == 4
    ]
    raise_count = len(raises)
    if raise_count == 0:
        pot_class = "limped_unraised" if saw_flop else "walk"
        aggressor = None
    elif raise_count == 1:
        pot_class = "single_raised"
        aggressor = raises[-1]["position"]
    elif raise_count == 2:
        pot_class = "three_bet"
        aggressor = raises[-1]["position"]
    else:
        pot_class = "four_bet_plus"
        aggressor = raises[-1]["position"]

    reached = showdown and result["committed"] == [starting_stack, starting_stack]
    all_in_street = result["ending_street"] if reached else None
    initiator = None
    for decision in hand["decisions"]:
        action = decision["applied"]
        if action["type"] == 4 and action["amount_to"] == (
            decision["my_street_contribution"] + decision["my_stack"]
        ):
            initiator = (decision["position"], decision["street"])
            break

    cbet_opportunity = False
    cbet_made = False
    if aggressor is not None:
        for decision in hand["decisions"]:
            if decision["street"] != 1:
                continue
            if decision["position"] == aggressor:
                cbet_opportunity = True
                cbet_made = decision["applied"]["type"] == 4
                break
            if decision["applied"]["type"] != 2:
                break

    preflop_actions = [[], []]
    for decision in hand["decisions"]:
        if decision["street"] == 0:
            preflop_actions[decision["position"]].append(
                decision["applied"]["type"]
            )
    return {
        "showdown": showdown,
        "saw_flop": saw_flop,
        "preflop_raise_count": raise_count,
        "pot_class": pot_class,
        "aggressor": aggressor,
        "all_in_reached": reached,
        "all_in_street": all_in_street,
        "initiator": initiator,
        "cbet_opportunity": cbet_opportunity,
        "cbet_made": cbet_made,
        "vpip": [any(action in (3, 4) for action in actions) for actions in preflop_actions],
        "pfr": [any(action == 4 for action in actions) for actions in preflop_actions],
    }


def random_key(match_key: str, hand_index: int, position: int) -> int:
    digest = hashlib.sha256(
        f"felt-random-key-v1:{match_key}:{hand_index}:{position}".encode()
    ).digest()
    return int.from_bytes(digest[:8], "big") & ((1 << 63) - 1)


def match_key(summary: dict[str, Any]) -> str:
    identity = {
        "summary_schema_version": summary["schema_version"],
        "harness_version": summary["harness_version"],
        "config": summary["config"],
        "bots": [bot["sha256"] for bot in summary["bots"]],
    }
    encoded = json.dumps(identity, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(b"felt-match-v1\0" + encoded).hexdigest()


def load_summary(directory: Path) -> dict[str, Any]:
    with (directory / "summary.json").open(encoding="utf-8") as source:
        summary = json.load(source)
    if summary.get("status") != "complete" or summary.get("result") is None:
        raise ValueError(f"{directory}: match summary is not complete")
    if len(summary.get("bots", [])) != 2:
        raise ValueError(f"{directory}: summary must contain two bots")
    return summary


def initialize_database(connection: sqlite3.Connection) -> None:
    connection.executescript(SCHEMA)
    found = connection.execute(
        "SELECT value FROM schema_meta WHERE key = 'schema_version'"
    ).fetchone()
    if found is not None and found[0] not in {"1", "2", str(SCHEMA_VERSION)}:
        raise ValueError(
            f"database schema {found[0]} is unsupported; expected 1, 2, or "
            f"{SCHEMA_VERSION}"
        )

    # Schema 2 changed reporting from bb/100 to bb/hand. Schema 3 adds ratings
    # storage. Recreate views on every open so old ledgers migrate without
    # rewriting any stored match or hand facts.
    connection.executescript(VIEWS)
    if found is None:
        connection.execute(
            "INSERT INTO schema_meta(key, value) VALUES('schema_version', ?)",
            (str(SCHEMA_VERSION),),
        )
    elif found[0] in {"1", "2"}:
        connection.execute(
            "UPDATE schema_meta SET value = ? WHERE key = 'schema_version'",
            (str(SCHEMA_VERSION),),
        )
    connection.commit()


def bot_id(connection: sqlite3.Connection, artifact: dict[str, Any]) -> int:
    connection.execute(
        "INSERT OR IGNORE INTO bots(sha256, name, path) VALUES(?, ?, ?)",
        (artifact["sha256"], artifact["name"], artifact["path"]),
    )
    connection.execute(
        "UPDATE bots SET name = ?, path = ? WHERE sha256 = ?",
        (artifact["name"], artifact["path"], artifact["sha256"]),
    )
    row = connection.execute(
        "SELECT id FROM bots WHERE sha256 = ?", (artifact["sha256"],)
    ).fetchone()
    return integer(row[0], "bot id")


def profile_id(connection: sqlite3.Connection, config: dict[str, Any]) -> int:
    values = (
        config["starting_stack"],
        config["small_blind"],
        config["big_blind"],
        config["decision_cap_us"],
        int(config["duplicate"]),
        int(config["equity_adjustment"]),
    )
    connection.execute(
        """INSERT OR IGNORE INTO rule_profiles(
             starting_stack, small_blind, big_blind, decision_cap_us,
             duplicate, equity_adjustment) VALUES(?, ?, ?, ?, ?, ?)""",
        values,
    )
    row = connection.execute(
        """SELECT id FROM rule_profiles WHERE
             starting_stack = ? AND small_blind = ? AND big_blind = ? AND
             decision_cap_us = ? AND duplicate = ? AND equity_adjustment = ?""",
        values,
    ).fetchone()
    return integer(row[0], "rule profile id")


def percentile_99(values: list[int]) -> int:
    if not values:
        return 0
    values.sort()
    return values[math.ceil(0.99 * len(values)) - 1]


def rebuild_statistics(connection: sqlite3.Connection, match_id: int) -> None:
    for table in (
        "match_bot_stats",
        "position_stats",
        "all_in_stats",
        "pot_class_stats",
        "action_stats",
        "timing_stats",
        "violation_stats",
        "hand_group_stats",
        "pair_results",
        "variance_stats",
    ):
        connection.execute(f"DELETE FROM {table} WHERE match_id = ?", (match_id,))

    connection.execute(
        """INSERT INTO match_bot_stats
        SELECT match_id, bot_slot, ?, COUNT(*), SUM(raw_net_chips),
               SUM(adjusted_net_chips),
               SUM(outcome = 'win'), SUM(outcome = 'loss'), SUM(outcome = 'chop'),
               SUM(saw_flop), SUM(showdown), SUM(showdown_win), SUM(vpip), SUM(pfr),
               SUM(cbet_opportunity), SUM(cbet_made), SUM(all_in_reached),
               SUM(all_in_initiated), SUM(exact_equity IS NOT NULL),
               AVG(exact_equity),
               SUM(CASE WHEN showdown THEN raw_net_chips ELSE 0 END),
               SUM(CASE WHEN showdown THEN adjusted_net_chips ELSE 0 END),
               SUM(CASE WHEN NOT showdown THEN raw_net_chips ELSE 0 END),
               SUM(CASE WHEN NOT showdown THEN adjusted_net_chips ELSE 0 END)
        FROM hand_players WHERE match_id = ? GROUP BY match_id, bot_slot""",
        (STATS_VERSION, match_id),
    )
    connection.execute(
        """INSERT INTO position_stats
        SELECT match_id, bot_slot, position, COUNT(*), SUM(raw_net_chips),
               SUM(adjusted_net_chips)
        FROM hand_players WHERE match_id = ?
        GROUP BY match_id, bot_slot, position""",
        (match_id,),
    )

    hands = connection.execute(
        "SELECT hand_count FROM matches WHERE id = ?", (match_id,)
    ).fetchone()[0]
    for bot_slot in range(2):
        for kind, column in (
            ("reached", "all_in_reached"),
            ("initiated", "all_in_initiated"),
        ):
            for street in (-1, 0, 1, 2, 3):
                street_column = (
                    "all_in_street"
                    if kind == "reached"
                    else "all_in_initiated_street"
                )
                condition = (
                    "" if street == -1 else f" AND {street_column} = ?"
                )
                parameters = (match_id, bot_slot) if street == -1 else (
                    match_id,
                    bot_slot,
                    street,
                )
                count = connection.execute(
                    f"SELECT COALESCE(SUM({column}), 0) FROM hand_players "
                    f"WHERE match_id = ? AND bot_slot = ?{condition}",
                    parameters,
                ).fetchone()[0]
                connection.execute(
                    "INSERT INTO all_in_stats VALUES(?, ?, ?, ?, ?, ?)",
                    (match_id, bot_slot, kind, street, count, hands),
                )
        for pot_class in POT_CLASSES:
            count = connection.execute(
                """SELECT COUNT(*) FROM hand_players
                   WHERE match_id = ? AND bot_slot = ? AND pot_class = ?""",
                (match_id, bot_slot, pot_class),
            ).fetchone()[0]
            connection.execute(
                "INSERT INTO pot_class_stats VALUES(?, ?, ?, ?, ?)",
                (match_id, bot_slot, pot_class, count, hands),
            )
        for street in STREETS:
            decisions = connection.execute(
                "SELECT COUNT(*) FROM actions WHERE match_id = ? AND bot_slot = ? AND street = ?",
                (match_id, bot_slot, street),
            ).fetchone()[0]
            for action_type in ACTION_TYPES:
                count = connection.execute(
                    """SELECT COUNT(*) FROM actions WHERE match_id = ? AND
                       bot_slot = ? AND street = ? AND applied_type = ?""",
                    (match_id, bot_slot, street, action_type),
                ).fetchone()[0]
                connection.execute(
                    "INSERT INTO action_stats VALUES(?, ?, ?, ?, ?, ?)",
                    (match_id, bot_slot, street, action_type, count, decisions),
                )

        timings = connection.execute(
            """SELECT cpu_time_ns, wall_time_ns, violation FROM actions
               WHERE match_id = ? AND bot_slot = ?""",
            (match_id, bot_slot),
        ).fetchall()
        cpu = [row[0] for row in timings]
        wall = [row[1] for row in timings]
        connection.execute(
            "INSERT INTO timing_stats VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (
                match_id,
                bot_slot,
                len(timings),
                statistics.fmean(cpu) if cpu else 0.0,
                percentile_99(cpu),
                max(cpu, default=0),
                statistics.fmean(wall) if wall else 0.0,
                percentile_99(wall),
                max(wall, default=0),
                sum(row[2] != 0 for row in timings),
            ),
        )
        decisions = len(timings)
        for violation in range(1, 6):
            count = connection.execute(
                """SELECT COUNT(*) FROM actions WHERE match_id = ? AND
                   bot_slot = ? AND violation = ?""",
                (match_id, bot_slot, violation),
            ).fetchone()[0]
            connection.execute(
                "INSERT INTO violation_stats VALUES(?, ?, ?, ?, ?)",
                (match_id, bot_slot, violation, count, decisions),
            )

    for group_type, column in (("bucket", "bucket"), ("combo", "exact_combo")):
        for position in (-1, 0, 1):
            position_filter = "" if position == -1 else " AND position = ?"
            parameters = (match_id,) if position == -1 else (match_id, position)
            rows = connection.execute(
                f"""SELECT match_id, bot_slot, {column}, COUNT(*),
                    SUM(raw_net_chips), SUM(adjusted_net_chips),
                    SUM(outcome = 'win'), SUM(outcome = 'loss'),
                    SUM(outcome = 'chop'), SUM(all_in_reached)
                    FROM hand_players WHERE match_id = ?{position_filter}
                    GROUP BY match_id, bot_slot, {column}""",
                parameters,
            ).fetchall()
            connection.executemany(
                """INSERT INTO hand_group_stats VALUES(
                     ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
                [
                    (row[0], row[1], group_type, row[2], position, *row[3:])
                    for row in rows
                ],
            )

    duplicate = connection.execute(
        """SELECT p.duplicate FROM matches m JOIN rule_profiles p
           ON p.id = m.rule_profile_id WHERE m.id = ?""",
        (match_id,),
    ).fetchone()[0]
    sample_expression = "h.pair_index" if duplicate else "h.hand_index"
    connection.execute(
        f"""INSERT INTO pair_results
        SELECT hp.match_id, {sample_expression}, hp.bot_slot, COUNT(*),
               SUM(hp.raw_net_chips), SUM(hp.adjusted_net_chips)
        FROM hand_players hp JOIN hands h
          ON h.match_id = hp.match_id AND h.hand_index = hp.hand_index
        WHERE hp.match_id = ?
        GROUP BY hp.match_id, {sample_expression}, hp.bot_slot""",
        (match_id,),
    )
    for bot_slot in range(2):
        rows = connection.execute(
            """SELECT raw_net_chips, adjusted_net_chips FROM pair_results
               WHERE match_id = ? AND bot_slot = ? ORDER BY sample_index""",
            (match_id, bot_slot),
        ).fetchall()
        for result_type, index in (("raw", 0), ("adjusted", 1)):
            values = [row[index] for row in rows]
            deviation = statistics.stdev(values) if len(values) > 1 else 0.0
            error = deviation / math.sqrt(len(values)) if values else 0.0
            connection.execute(
                "INSERT INTO variance_stats VALUES(?, ?, ?, ?, ?, ?, ?, ?)",
                (
                    match_id,
                    bot_slot,
                    result_type,
                    len(values),
                    2 if duplicate else 1,
                    statistics.fmean(values) if values else 0.0,
                    deviation,
                    error,
                ),
            )


def validate_totals(
    connection: sqlite3.Connection,
    match_id: int,
    summary: dict[str, Any],
) -> None:
    expected = summary["result"]
    for slot in range(2):
        row = connection.execute(
            """SELECT COUNT(*), SUM(raw_net_chips), SUM(adjusted_net_chips),
                      SUM(position = 0),
                      SUM(CASE WHEN position = 0 THEN raw_net_chips ELSE 0 END),
                      SUM(CASE WHEN position = 1 THEN raw_net_chips ELSE 0 END),
                      SUM(CASE WHEN position = 0 THEN adjusted_net_chips ELSE 0 END),
                      SUM(CASE WHEN position = 1 THEN adjusted_net_chips ELSE 0 END)
               FROM hand_players WHERE match_id = ? AND bot_slot = ?""",
            (match_id, slot),
        ).fetchone()
        if row[0] != summary["config"]["hand_count"]:
            raise ValueError("database hand count does not match summary")
        if [row[1], row[2]] != [
            expected["raw_net_by_bot"][slot],
            expected["adjusted_net_by_bot"][slot],
        ]:
            raise ValueError("database bot totals do not match summary")
        if [row[4], row[5]] != expected["raw_net_by_bot_and_position"][slot]:
            raise ValueError("database raw position totals do not match summary")
        if [row[6], row[7]] != expected["adjusted_net_by_bot_and_position"][slot]:
            raise ValueError("database adjusted position totals do not match summary")


def validate_statistics(
    connection: sqlite3.Connection,
    match_id: int,
    summary: dict[str, Any],
) -> None:
    expected_hands = summary["config"]["hand_count"]
    for slot in range(2):
        row = connection.execute(
            """SELECT hands, raw_net_chips, adjusted_net_chips,
                      wins + losses + chops
               FROM match_bot_stats WHERE match_id = ? AND bot_slot = ?""",
            (match_id, slot),
        ).fetchone()
        if row is None or row[0] != expected_hands or row[3] != expected_hands:
            raise ValueError("headline statistics do not cross-foot")
        if list(row[1:3]) != [
            summary["result"]["raw_net_by_bot"][slot],
            summary["result"]["adjusted_net_by_bot"][slot],
        ]:
            raise ValueError("headline statistics do not match summary")
        position = connection.execute(
            """SELECT SUM(hands), SUM(raw_net_chips), SUM(adjusted_net_chips)
               FROM position_stats WHERE match_id = ? AND bot_slot = ?""",
            (match_id, slot),
        ).fetchone()
        if tuple(position) != (expected_hands, row[1], row[2]):
            raise ValueError("position statistics do not cross-foot")
        for group_type in ("bucket", "combo"):
            grouped = connection.execute(
                """SELECT SUM(hands), SUM(raw_net_chips),
                          SUM(adjusted_net_chips)
                   FROM hand_group_stats WHERE match_id = ? AND bot_slot = ?
                   AND group_type = ? AND position = -1""",
                (match_id, slot, group_type),
            ).fetchone()
            if tuple(grouped) != (expected_hands, row[1], row[2]):
                raise ValueError(f"{group_type} statistics do not cross-foot")
        decisions = connection.execute(
            "SELECT COUNT(*) FROM actions WHERE match_id = ? AND bot_slot = ?",
            (match_id, slot),
        ).fetchone()[0]
        timing_decisions = connection.execute(
            """SELECT decisions FROM timing_stats
               WHERE match_id = ? AND bot_slot = ?""",
            (match_id, slot),
        ).fetchone()[0]
        if decisions != timing_decisions:
            raise ValueError("timing statistics do not cross-foot")


def import_match(
    directory: Path,
    database: Path,
    replace: bool = False,
    keep_hand_log: bool = False,
) -> tuple[int, int]:
    directory = directory.resolve()
    summary = load_summary(directory)
    source_path = hand_log_path(directory)
    config = summary["config"]
    expected_hands = integer(config["hand_count"], "hand_count")
    starting_stack = integer(config["starting_stack"], "starting_stack")
    key = match_key(summary)
    database.parent.mkdir(parents=True, exist_ok=True)
    connection = sqlite3.connect(database)
    connection.execute("PRAGMA foreign_keys = ON")
    connection.execute("PRAGMA journal_mode = WAL")
    connection.execute("PRAGMA synchronous = NORMAL")
    initialize_database(connection)

    try:
        connection.execute("BEGIN IMMEDIATE")
        existing = connection.execute(
            "SELECT id FROM matches WHERE match_key = ?", (key,)
        ).fetchone()
        if existing is not None:
            if not replace:
                raise ValueError(
                    f"match already exists as id {existing[0]}; use --replace"
                )
            connection.execute("DELETE FROM matches WHERE id = ?", (existing[0],))

        bot_ids = [bot_id(connection, artifact) for artifact in summary["bots"]]
        rules = profile_id(connection, config)
        cursor = connection.execute(
            """INSERT INTO matches(
                 match_key, match_seed, hand_count, rule_profile_id,
                 summary_schema_version, harness_version, source_directory,
                 summary_json) VALUES(?, ?, ?, ?, ?, ?, ?, ?)""",
            (
                key,
                str(config["match_seed"]),
                expected_hands,
                rules,
                summary["schema_version"],
                summary["harness_version"],
                str(directory),
                json.dumps(summary, sort_keys=True, separators=(",", ":")),
            ),
        )
        match_id = integer(cursor.lastrowid, "match id")
        result_summary = summary["result"]
        for slot in range(2):
            connection.execute(
                "INSERT INTO match_players VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)",
                (
                    match_id,
                    slot,
                    bot_ids[slot],
                    result_summary["raw_net_by_bot"][slot],
                    result_summary["adjusted_net_by_bot"][slot],
                    *result_summary["raw_net_by_bot_and_position"][slot],
                    *result_summary["adjusted_net_by_bot_and_position"][slot],
                ),
            )

        chunk_lines: list[bytes] = []
        chunk_index = 0
        first_chunk_hand = 0
        hands_read = 0

        def flush_chunk() -> None:
            nonlocal chunk_lines, chunk_index, first_chunk_hand
            if not chunk_lines:
                return
            connection.execute(
                "INSERT INTO hand_chunks VALUES(?, ?, ?, ?, 'zlib-jsonl', ?)",
                (
                    match_id,
                    chunk_index,
                    first_chunk_hand,
                    len(chunk_lines),
                    zlib.compress(b"".join(chunk_lines), level=6),
                ),
            )
            chunk_index += 1
            first_chunk_hand += len(chunk_lines)
            chunk_lines = []

        with open_hand_log(source_path) as source:
            for line_number, line in enumerate(source, 1):
                if not line.strip():
                    raise ValueError(f"empty hand-log line {line_number}")
                hand = json.loads(line)
                hand_index = integer(hand["hand_index"], "hand index")
                if hand_index != hands_read:
                    raise ValueError(f"noncontiguous hand index at line {line_number}")
                mapping = hand["bot_by_position"]
                if sorted(mapping) != [0, 1]:
                    raise ValueError(f"invalid bot mapping at line {line_number}")
                result = hand["result"]
                raw_net = result["raw_net"]
                adjusted_net = result["adjusted_net"]
                if sum(raw_net) != 0 or sum(adjusted_net) != 0:
                    raise ValueError(f"non-zero-sum result at line {line_number}")
                features = hand_features(hand, starting_stack)
                outcomes = raw_outcomes(result)
                final_pot = sum(result["committed"])
                folded = result["folded_position"]
                pair_index = hand["pair_index"]
                board = hand["board"]
                holes = hand["hole_cards"]
                if len(board) != 5 or len(holes) != 2:
                    raise ValueError(f"invalid cards at line {line_number}")
                for card in board + holes[0] + holes[1]:
                    card_text(integer(card, "card"))
                history_line = len(chunk_lines)
                chunk_lines.append(line.rstrip("\r\n").encode() + b"\n")

                connection.execute(
                    """INSERT INTO hands VALUES(
                    ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?,
                    ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
                    (
                        match_id,
                        hand_index,
                        pair_index,
                        hand["deal_index"],
                        mapping[0],
                        mapping[1],
                        *holes[0],
                        *holes[1],
                        *board,
                        result["reason"],
                        result["ending_street"],
                        folded,
                        features["preflop_raise_count"],
                        features["pot_class"],
                        int(features["saw_flop"]),
                        int(features["showdown"]),
                        features["all_in_street"],
                        None if features["initiator"] is None else features["initiator"][0],
                        None if features["initiator"] is None else features["initiator"][1],
                        final_pot,
                        raw_net[0],
                        raw_net[1],
                        adjusted_net[0],
                        adjusted_net[1],
                        chunk_index,
                        history_line,
                        random_key(key, hand_index, 2),
                    ),
                )

                for position in range(2):
                    slot = mapping[position]
                    equity = result.get("equity")
                    exact_equity = None
                    if equity is not None:
                        boards = integer(equity["boards"], "equity boards")
                        wins = integer(equity["wins"][position], "equity wins")
                        ties = integer(equity["ties"], "equity ties")
                        exact_equity = (2 * wins + ties) / (2 * boards)
                    connection.execute(
                        """INSERT INTO hand_players VALUES(
                        ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?,
                        ?, ?, ?, ?, ?, ?, ?)""",
                        (
                            match_id,
                            hand_index,
                            slot,
                            bot_ids[slot],
                            bot_ids[1 - slot],
                            position,
                            bucket_label(holes[position]),
                            combo_label(holes[position]),
                            outcomes[position],
                            raw_net[position],
                            adjusted_net[position],
                            int(features["vpip"][position]),
                            int(features["pfr"][position]),
                            int(features["saw_flop"]),
                            int(features["showdown"]),
                            int(features["showdown"] and outcomes[position] == "win"),
                            int(features["cbet_opportunity"] and features["aggressor"] == position),
                            int(features["cbet_made"] and features["aggressor"] == position),
                            int(features["all_in_reached"]),
                            int(features["initiator"] is not None and features["initiator"][0] == position),
                            features["all_in_street"],
                            (
                                features["initiator"][1]
                                if features["initiator"] is not None
                                and features["initiator"][0] == position
                                else None
                            ),
                            features["pot_class"],
                            final_pot,
                            exact_equity,
                            random_key(key, hand_index, position),
                        ),
                    )

                for decision_index, decision in enumerate(hand["decisions"]):
                    position = integer(decision["position"], "decision position")
                    street = integer(decision["street"], "decision street")
                    if position not in (0, 1) or street not in STREETS:
                        raise ValueError(f"invalid decision at line {line_number}")
                    connection.execute(
                        "INSERT INTO actions VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                        (
                            match_id,
                            hand_index,
                            decision_index,
                            mapping[position],
                            position,
                            street,
                            decision["requested"]["type"],
                            decision["applied"]["type"],
                            decision["applied"]["amount_to"],
                            decision["violation"],
                            decision["pot"],
                            decision["to_call"],
                            decision["cpu_time_ns"],
                            decision["wall_time_ns"],
                        ),
                    )

                hands_read += 1
                if len(chunk_lines) == CHUNK_HANDS:
                    flush_chunk()
        flush_chunk()
        if hands_read != expected_hands:
            raise ValueError(
                f"expected {expected_hands} hands but read {hands_read}"
            )
        validate_totals(connection, match_id, summary)
        rebuild_statistics(connection, match_id)
        validate_statistics(connection, match_id, summary)
        connection.commit()
    except Exception:
        connection.rollback()
        raise
    finally:
        connection.close()

    if not keep_hand_log:
        for name in ("hands.jsonl", "hands.jsonl.gz", "stats.json"):
            path = directory / name
            if path.exists():
                path.unlink()
    return match_id, expected_hands


def discover(paths: Iterable[Path]) -> list[Path]:
    found: set[Path] = set()
    for path in paths:
        if (path / "summary.json").is_file():
            found.add(path.resolve())
        elif path.is_dir():
            found.update(
                summary.parent.resolve()
                for summary in path.rglob("summary.json")
                if (summary.parent / "hands.jsonl").is_file()
                or (summary.parent / "hands.jsonl.gz").is_file()
            )
        else:
            raise ValueError(f"{path}: not a match directory or directory tree")
    return sorted(found)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Finalize Felt matches into a local SQLite ledger."
    )
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument(
        "--database", type=Path, default=Path("data/felt.sqlite3")
    )
    parser.add_argument("--replace", action="store_true")
    parser.add_argument("--keep-hand-log", action="store_true")
    arguments = parser.parse_args()
    try:
        directories = discover(arguments.paths)
        for directory in directories:
            match_id, hands = import_match(
                directory,
                arguments.database,
                replace=arguments.replace,
                keep_hand_log=arguments.keep_hand_log,
            )
            print(
                f"finalized match_id={match_id} hands={hands} "
                f"database={arguments.database}"
            )
    except (OSError, ValueError, KeyError, json.JSONDecodeError, sqlite3.Error) as error:
        parser.exit(1, f"finalize_match: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
