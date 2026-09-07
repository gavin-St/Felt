#!/usr/bin/env python3
"""Serve the hand ledger to the web app, on this machine only.

The dashboard is a build-time snapshot: `web/lib/dashboard.ts` imports
`data/dashboard.json` and the whole thing is compiled into the bundle. That
works for a few hundred matchup rows and cannot work for 4.6 million hands,
and the deployed site is a Cloudflare Worker with no filesystem, so it can
never open the ledger itself.

So the replay browser talks to this instead. It is stdlib only, read-only, and
binds to the loopback interface, and the web app only looks for it when
`VITE_HAND_REPLAY` is on -- which it is in `npm run dev` and is not in a
production build. The published site therefore has no replay at all, by
construction rather than by a check that could be forgotten.

    python3 scripts/hand_server.py --database data/felt.sqlite3

Endpoints:
    /api/meta                      bots, matchups, blinds
    /api/hands?...                 filtered, paged search
    /api/hand/<match_id>/<index>   one hand, with every decision the bots saw
"""

from __future__ import annotations

import argparse
import json
import re
import sqlite3
import zlib
from functools import lru_cache
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

RANKS = "23456789TJQKA"
SUITS = "cdsh"
MAX_LIMIT = 100

# Every filter is a fragment of SQL plus the table it needs. Keeping them in
# one table is what stops the query builder from growing an if for each one,
# and what makes the set the page offers and the set the server honours the
# same list.
FILTERS: dict[str, str] = {
    "showdown": "hp.showdown = 1",
    "no-showdown": "hp.showdown = 0 AND hp.saw_flop = 1",
    "preflop": "hp.saw_flop = 0",
    "all-in": "hp.all_in_reached = 1",
    "three-bet": "hp.pot_class IN ('three_bet', 'four_bet_plus')",
    "big-pot": "hp.final_pot_chips >= 4000",
    "won": "hp.outcome = 'win'",
    "lost": "hp.outcome = 'loss'",
    "cbet": "hp.cbet_made = 1",
    "opponent-folded": "h.end_reason = 1 AND h.folded_position <> hp.position",
    "hero-folded": "h.end_reason = 1 AND h.folded_position = hp.position",
}

SORTS: dict[str, str] = {
    "random": "hp.random_key",
    "pot": "hp.final_pot_chips DESC",
    "won-most": "hp.raw_net_chips DESC",
    "lost-most": "hp.raw_net_chips ASC",
    "hand-order": "hp.match_id, hp.hand_index",
}

BUCKET = re.compile(r"^[2-9TJQKA]{2}[so]?$", re.IGNORECASE)


def card(value: int | None) -> str | None:
    if value is None or not 0 <= value < 52:
        return None
    return RANKS[value // 4] + SUITS[value % 4]


def board(row: dict, prefix: str = "board_") -> list[str]:
    out = []
    for index in range(1, 6):
        text = card(row.get(f"{prefix}{index}"))
        if text is None:
            break
        out.append(text)
    return out


def normalise_bucket(text: str) -> str | None:
    text = text.strip()
    if not BUCKET.match(text):
        return None
    head = text[:2].upper()
    tail = text[2:].lower()
    if head[0] == head[1]:
        return head
    return head + (tail or "o")


class Ledger:
    def __init__(self, database: Path) -> None:
        self.database = database.resolve()
        if not self.database.exists():
            raise SystemExit(f"hand_server: no ledger at {self.database}")

    def connect(self) -> sqlite3.Connection:
        connection = sqlite3.connect(f"file:{self.database}?mode=ro", uri=True)
        connection.row_factory = sqlite3.Row
        return connection

    @lru_cache(maxsize=1)
    def meta(self) -> dict:
        with self.connect() as connection:
            profile = connection.execute(
                """SELECT rp.big_blind, rp.small_blind, rp.starting_stack
                   FROM rule_profiles rp JOIN matches m ON m.rule_profile_id = rp.id
                   GROUP BY rp.id ORDER BY COUNT(m.id) DESC LIMIT 1"""
            ).fetchone()
            bots = [
                dict(row)
                for row in connection.execute(
                    """SELECT b.id, b.name, COUNT(DISTINCT mp.match_id) AS matches,
                              SUM(m.hand_count) AS hands
                       FROM bots b
                       JOIN match_players mp ON mp.bot_id = b.id
                       JOIN matches m ON m.id = mp.match_id
                       GROUP BY b.id ORDER BY b.name"""
                )
            ]
            matchups = [
                dict(row)
                for row in connection.execute(
                    """SELECT m.id AS match_id, m.hand_count,
                              a.bot_id AS bot_id, ba.name AS bot_name,
                              b.bot_id AS opponent_bot_id, bb.name AS opponent_name
                       FROM matches m
                       JOIN match_players a ON a.match_id = m.id AND a.bot_slot = 0
                       JOIN match_players b ON b.match_id = m.id AND b.bot_slot = 1
                       JOIN bots ba ON ba.id = a.bot_id
                       JOIN bots bb ON bb.id = b.bot_id
                       ORDER BY ba.name, bb.name"""
                )
            ]
        return {
            "big_blind": profile["big_blind"],
            "small_blind": profile["small_blind"],
            "starting_stack": profile["starting_stack"],
            "bots": bots,
            "matchups": matchups,
            "filters": sorted(FILTERS),
            "sorts": sorted(SORTS),
        }

    def search(self, query: dict[str, list[str]]) -> dict:
        def one(name: str) -> str | None:
            values = query.get(name)
            return values[0] if values else None

        where = ["1 = 1"]
        values: list[object] = []
        if one("bot"):
            where.append("hp.bot_id = ?")
            values.append(int(one("bot")))
        if one("opponent"):
            where.append("hp.opponent_bot_id = ?")
            values.append(int(one("opponent")))
        if one("match"):
            where.append("hp.match_id = ?")
            values.append(int(one("match")))
        if one("hand"):
            bucket = normalise_bucket(one("hand") or "")
            if bucket is None:
                raise ValueError("a starting hand looks like KK, AKs or T9o")
            where.append("hp.bucket = ?")
            values.append(bucket)
        for tag in query.get("filter", []):
            for part in tag.split(","):
                part = part.strip()
                if not part:
                    continue
                if part not in FILTERS:
                    raise ValueError(f"unknown filter {part!r}")
                where.append(FILTERS[part])

        order = SORTS.get(one("sort") or "random")
        if order is None:
            raise ValueError(f"unknown sort {one('sort')!r}")
        limit = max(1, min(MAX_LIMIT, int(one("limit") or 20)))
        offset = max(0, int(one("offset") or 0))
        clause = " AND ".join(where)

        with self.connect() as connection:
            total = connection.execute(
                f"""SELECT COUNT(*) FROM hand_players hp
                    JOIN hands h ON h.match_id = hp.match_id
                                AND h.hand_index = hp.hand_index
                    WHERE {clause}""",
                values,
            ).fetchone()[0]
            rows = [
                dict(row)
                for row in connection.execute(
                    f"""SELECT hp.match_id, hp.hand_index, hp.bot_id, hp.opponent_bot_id,
                               hp.position, hp.bucket, hp.exact_combo, hp.outcome,
                               hp.raw_net_chips, hp.adjusted_net_chips, hp.showdown,
                               hp.showdown_win, hp.all_in_reached, hp.cbet_made,
                               hp.pot_class, hp.final_pot_chips, hp.saw_flop,
                               h.ending_street, h.end_reason, h.folded_position,
                               h.board_1, h.board_2, h.board_3, h.board_4, h.board_5,
                               ba.name AS bot_name, bb.name AS opponent_name
                        FROM hand_players hp
                        JOIN hands h ON h.match_id = hp.match_id
                                    AND h.hand_index = hp.hand_index
                        JOIN bots ba ON ba.id = hp.bot_id
                        JOIN bots bb ON bb.id = hp.opponent_bot_id
                        WHERE {clause}
                        ORDER BY {order}
                        LIMIT ? OFFSET ?""",
                    [*values, limit, offset],
                )
            ]
        for row in rows:
            row["board"] = board(row)
            for index in range(1, 6):
                row.pop(f"board_{index}", None)
        return {"total": total, "limit": limit, "offset": offset, "hands": rows}

    def hand(self, match_id: int, hand_index: int) -> dict:
        with self.connect() as connection:
            chunk = connection.execute(
                """SELECT jsonl, codec, first_hand_index FROM hand_chunks
                   WHERE match_id = ? AND first_hand_index <= ?
                   ORDER BY first_hand_index DESC LIMIT 1""",
                (match_id, hand_index),
            ).fetchone()
            if chunk is None:
                raise KeyError("no such hand")
            if chunk["codec"] != "zlib-jsonl":
                raise ValueError(f"unsupported chunk codec {chunk['codec']!r}")
            lines = zlib.decompress(chunk["jsonl"]).decode("utf-8").splitlines()
            offset = hand_index - chunk["first_hand_index"]
            if not 0 <= offset < len(lines):
                raise KeyError("no such hand")
            log = json.loads(lines[offset])

            players = [
                dict(row)
                for row in connection.execute(
                    """SELECT hp.bot_slot, hp.bot_id, b.name, hp.position, hp.bucket,
                              hp.exact_combo, hp.outcome, hp.raw_net_chips,
                              hp.adjusted_net_chips, hp.showdown_win
                       FROM hand_players hp JOIN bots b ON b.id = hp.bot_id
                       WHERE hp.match_id = ? AND hp.hand_index = ?
                       ORDER BY hp.bot_slot""",
                    (match_id, hand_index),
                )
            ]
            summary = dict(
                connection.execute(
                    """SELECT h.end_reason, h.ending_street, h.folded_position,
                              h.pot_class, h.showdown, h.final_pot_chips,
                              h.preflop_raise_count, h.all_in_street, m.hand_count,
                              rp.big_blind, rp.small_blind, rp.starting_stack
                       FROM hands h
                       JOIN matches m ON m.id = h.match_id
                       JOIN rule_profiles rp ON rp.id = m.rule_profile_id
                       WHERE h.match_id = ? AND h.hand_index = ?""",
                    (match_id, hand_index),
                ).fetchone()
            )

        # bot_by_position[position] is the slot sitting there this hand.
        by_position = log.get("bot_by_position", [0, 1])
        for player in players:
            player["hole"] = [
                card(value) for value in log["hole_cards"][by_position.index(player["bot_slot"])]
            ]
        return {
            "match_id": match_id,
            "hand_index": hand_index,
            "board": [card(value) for value in log.get("board", []) if card(value)],
            "bot_by_position": by_position,
            "events": log.get("events", []),
            "decisions": log.get("decisions", []),
            "players": players,
            "summary": summary,
        }


class Handler(BaseHTTPRequestHandler):
    ledger: Ledger

    protocol_version = "HTTP/1.1"

    def log_message(self, *_args) -> None:  # quiet; this runs beside a dev server
        pass

    def _send(self, status: int, payload: dict) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self) -> None:  # noqa: N802
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        route = parsed.path.rstrip("/") or "/"
        try:
            if route == "/api/meta":
                return self._send(200, self.ledger.meta())
            if route == "/api/hands":
                return self._send(200, self.ledger.search(parse_qs(parsed.query)))
            parts = route.split("/")
            if len(parts) == 5 and parts[1:3] == ["api", "hand"]:
                return self._send(
                    200, self.ledger.hand(int(parts[3]), int(parts[4]))
                )
            return self._send(404, {"error": f"no route {route}"})
        except KeyError as error:
            self._send(404, {"error": error.args[0]})
        except (ValueError, sqlite3.Error) as error:
            self._send(400, {"error": str(error)})


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", type=Path, default=Path("data/felt.sqlite3"))
    parser.add_argument("--port", type=int, default=8899)
    parser.add_argument("--host", default="127.0.0.1")
    arguments = parser.parse_args()

    Handler.ledger = Ledger(arguments.database)
    server = ThreadingHTTPServer((arguments.host, arguments.port), Handler)
    print(
        f"hand_server: {arguments.database} on http://{arguments.host}:{arguments.port}"
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
