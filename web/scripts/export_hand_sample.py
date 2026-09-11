#!/usr/bin/env python3
"""Write a browsable sample of the hand ledger for the published site.

The ledger is 8 GB and its compressed hand logs alone are 1.2 GB, which is
more than GitHub Pages will host and far more than a git repository should
carry. A sample is not: two hundred hands per match is about 30 kB gzipped,
twelve megabytes for all four hundred matches, and it is enough to browse,
filter and replay.

Hands that saw a flop are preferred, because a hand that ended before the flop
has nothing to replay. Within those the pick is by random_key -- the ledger's
own stable shuffle -- so the sample is uniform, reproducible, and the same
hands come back after a rerun of an unrelated match.

    python3 web/scripts/export_hand_sample.py --database data/felt.sqlite3
"""

from __future__ import annotations

import argparse
import gzip
import json
import sqlite3
import zlib
from pathlib import Path

RANKS = "23456789TJQKA"
SUITS = "cdsh"
SAMPLE = 200


def card(value: int | None) -> str | None:
    if value is None or not 0 <= value < 52:
        return None
    return RANKS[value // 4] + SUITS[value % 4]


def rows(connection: sqlite3.Connection, sql: str, values=()) -> list[dict]:
    return [dict(row) for row in connection.execute(sql, values)]


def sample_indexes(connection: sqlite3.Connection, match_id: int) -> list[int]:
    """The hands to publish, flop-seeing first, by the ledger's own shuffle."""
    picked = [
        row["hand_index"]
        for row in connection.execute(
            """SELECT hand_index FROM hands WHERE match_id = ? AND saw_flop = 1
               ORDER BY random_key LIMIT ?""",
            (match_id, SAMPLE),
        )
    ]
    if len(picked) < SAMPLE:
        picked += [
            row["hand_index"]
            for row in connection.execute(
                """SELECT hand_index FROM hands WHERE match_id = ? AND saw_flop = 0
                   ORDER BY random_key LIMIT ?""",
                (match_id, SAMPLE - len(picked)),
            )
        ]
    return sorted(picked)


def summaries(connection: sqlite3.Connection, match_id: int,
              indexes: list[int]) -> list[dict]:
    """One row per player per hand, the shape /api/hands returns.

    Every column a filter reads comes along, so the published site can apply
    the same filters without the SQL that defined them.
    """
    marks = ",".join("?" * len(indexes))
    found = rows(
        connection,
        f"""SELECT hp.match_id, hp.hand_index, hp.bot_id, hp.opponent_bot_id,
                   hp.position, hp.bucket, hp.exact_combo, hp.outcome,
                   hp.raw_net_chips, hp.adjusted_net_chips, hp.showdown,
                   hp.showdown_win, hp.all_in_reached, hp.all_in_street,
                   hp.bluffed, hp.cbet_made, hp.pot_class, hp.final_pot_chips,
                   hp.saw_flop, h.ending_street, h.end_reason, h.folded_position,
                   h.board_1, h.board_2, h.board_3, h.board_4, h.board_5,
                   ba.name AS bot_name, bb.name AS opponent_name
            FROM hand_players hp
            JOIN hands h ON h.match_id = hp.match_id
                        AND h.hand_index = hp.hand_index
            JOIN bots ba ON ba.id = hp.bot_id
            JOIN bots bb ON bb.id = hp.opponent_bot_id
            WHERE hp.match_id = ? AND hp.hand_index IN ({marks})""",
        (match_id, *indexes),
    )
    bluffed = {
        (row["hand_index"], row["position"]): row["bluffed"] for row in found
    }
    for row in found:
        row["board"] = [
            value
            for value in (card(row.pop(f"board_{n}")) for n in range(1, 6))
            if value
        ]
        other = 1 - row["position"]
        row["opponent_bluffed"] = bluffed.get((row["hand_index"], other), 0)
    return found


def details(connection: sqlite3.Connection, match_id: int,
            indexes: list[int]) -> dict[str, dict]:
    """The replay payload, the shape /api/hand/<match>/<index> returns."""
    wanted = set(indexes)
    players_by_hand: dict[int, list[dict]] = {}
    marks = ",".join("?" * len(indexes))
    for row in rows(
        connection,
        f"""SELECT hp.hand_index, hp.bot_slot, hp.bot_id, b.name, hp.position,
                   hp.bucket, hp.exact_combo, hp.outcome, hp.raw_net_chips,
                   hp.adjusted_net_chips, hp.showdown_win, hp.exact_equity
            FROM hand_players hp JOIN bots b ON b.id = hp.bot_id
            WHERE hp.match_id = ? AND hp.hand_index IN ({marks})
            ORDER BY hp.hand_index, hp.bot_slot""",
        (match_id, *indexes),
    ):
        players_by_hand.setdefault(row.pop("hand_index"), []).append(row)

    bluffs: dict[int, dict[int, int]] = {}
    for row in connection.execute(
        f"""SELECT hand_index, decision_index, bluff FROM actions
            WHERE match_id = ? AND hand_index IN ({marks})""",
        (match_id, *indexes),
    ):
        bluffs.setdefault(row[0], {})[row[1]] = row[2]

    summary_by_hand = {
        row.pop("hand_index"): row
        for row in rows(
            connection,
            f"""SELECT h.hand_index, h.end_reason, h.ending_street,
                       h.folded_position, h.pot_class, h.showdown,
                       h.final_pot_chips, h.preflop_raise_count, h.all_in_street,
                       m.hand_count, rp.big_blind, rp.small_blind,
                       rp.starting_stack
                FROM hands h
                JOIN matches m ON m.id = h.match_id
                JOIN rule_profiles rp ON rp.id = m.rule_profile_id
                WHERE h.match_id = ? AND h.hand_index IN ({marks})""",
            (match_id, *indexes),
        )
    }

    out: dict[str, dict] = {}
    for chunk in connection.execute(
        "SELECT first_hand_index, codec, jsonl FROM hand_chunks "
        "WHERE match_id = ? ORDER BY first_hand_index",
        (match_id,),
    ):
        if chunk["codec"] != "zlib-jsonl":
            raise ValueError(f"unsupported codec {chunk['codec']!r}")
        first = chunk["first_hand_index"]
        lines = zlib.decompress(chunk["jsonl"]).decode("utf-8").splitlines()
        for offset, line in enumerate(lines):
            index = first + offset
            if index not in wanted:
                continue
            log = json.loads(line)
            by_position = log.get("bot_by_position", [0, 1])
            decisions = [dict(item) for item in log.get("decisions", [])]
            for decision_index, bluff in bluffs.get(index, {}).items():
                if 0 <= decision_index < len(decisions):
                    decisions[decision_index]["bluff"] = bool(bluff)
            players = [dict(item) for item in players_by_hand.get(index, [])]
            for player in players:
                seat = by_position.index(player["bot_slot"])
                player["hole"] = [card(v) for v in log["hole_cards"][seat]]
            out[str(index)] = {
                "match_id": match_id,
                "hand_index": index,
                "board": [c for c in (card(v) for v in log.get("board", [])) if c],
                "bot_by_position": by_position,
                "events": log.get("events", []),
                "decisions": decisions,
                "players": players,
                "summary": summary_by_hand.get(index, {}),
            }
    return out


def write(path: Path, payload: dict) -> int:
    text = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    blob = gzip.compress(text, 9, mtime=0)
    if path.exists() and path.read_bytes() == blob:
        return len(blob)
    path.write_bytes(blob)
    return len(blob)


DEFAULT_OUT = Path("web/public/data/hands")


def export(database: Path, out: Path = DEFAULT_OUT, limit: int = 0) -> int:
    """Write the sample, and return the bytes it takes on disk.

    Opened read-only. When the ledger has been checkpointed -- no -wal beside
    it -- immutable is added as well, which takes no locks and cannot create a
    file; with a -wal present that would read straight past it, so it is left
    off and SQLite reads the log properly.
    """
    wal = database.with_name(database.name + "-wal")
    immutable = "&immutable=1" if not wal.exists() or wal.stat().st_size == 0 else ""
    connection = sqlite3.connect(f"file:{database}?mode=ro{immutable}", uri=True)
    connection.row_factory = sqlite3.Row

    out.mkdir(parents=True, exist_ok=True)

    profile = connection.execute(
        """SELECT rp.big_blind, rp.small_blind, rp.starting_stack
           FROM rule_profiles rp JOIN matches m ON m.rule_profile_id = rp.id
           GROUP BY rp.id ORDER BY COUNT(m.id) DESC LIMIT 1"""
    ).fetchone()
    meta = {
        "big_blind": profile["big_blind"],
        "small_blind": profile["small_blind"],
        "starting_stack": profile["starting_stack"],
        "sample": SAMPLE,
        "bots": rows(
            connection,
            """SELECT b.id, b.name, COUNT(DISTINCT mp.match_id) AS matches,
                      SUM(m.hand_count) AS hands
               FROM bots b
               JOIN match_players mp ON mp.bot_id = b.id
               JOIN matches m ON m.id = mp.match_id
               GROUP BY b.id ORDER BY b.name""",
        ),
        "matchups": rows(
            connection,
            """SELECT m.id AS match_id, m.hand_count,
                      a.bot_id AS bot_id, ba.name AS bot_name,
                      b.bot_id AS opponent_bot_id, bb.name AS opponent_name
               FROM matches m
               JOIN match_players a ON a.match_id = m.id AND a.bot_slot = 0
               JOIN match_players b ON b.match_id = m.id AND b.bot_slot = 1
               JOIN bots ba ON ba.id = a.bot_id
               JOIN bots bb ON bb.id = b.bot_id
               ORDER BY ba.name, bb.name""",
        ),
    }
    total = write(out / "meta.json.gz", meta)

    match_ids = [row[0] for row in connection.execute(
        "SELECT id FROM matches ORDER BY id")]
    if limit:
        match_ids = match_ids[:limit]

    written = set()
    for position, match_id in enumerate(match_ids, 1):
        indexes = sample_indexes(connection, match_id)
        if not indexes:
            continue
        payload = {
            "match_id": match_id,
            "hand_count": connection.execute(
                "SELECT hand_count FROM matches WHERE id = ?", (match_id,)
            ).fetchone()[0],
            "sampled": len(indexes),
            "summaries": summaries(connection, match_id, indexes),
            "hands": details(connection, match_id, indexes),
        }
        name = f"{match_id}.json.gz"
        written.add(name)
        total += write(out / name, payload)
        if position % 50 == 0 or position == len(match_ids):
            print(f"  {position}/{len(match_ids)} matches, "
                  f"{total / 1e6:.1f} MB so far", flush=True)

    if not limit:
        for stale in out.glob("*.json.gz"):
            if stale.name != "meta.json.gz" and stale.name not in written:
                stale.unlink()

    connection.close()
    print(f"wrote {len(written)} matches, {total / 1e6:.1f} MB into {out}")
    return total


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", type=Path, default=Path("data/felt.sqlite3"))
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--matches", type=int, default=0,
                        help="stop after this many matches, for a dry run")
    arguments = parser.parse_args()
    export(arguments.database, arguments.out, arguments.matches)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
