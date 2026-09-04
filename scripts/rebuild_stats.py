#!/usr/bin/env python3
"""Recalculate Felt's derived statistics entirely from SQLite facts."""

from __future__ import annotations

import argparse
import json
import sqlite3
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from finalize_match import (  # noqa: E402
    initialize_database,
    rebuild_statistics,
    validate_statistics,
    validate_totals,
)


def rebuild(database: Path, match_ids: list[int]) -> list[int]:
    connection = sqlite3.connect(database)
    connection.execute("PRAGMA foreign_keys = ON")
    initialize_database(connection)
    if not match_ids:
        match_ids = [row[0] for row in connection.execute("SELECT id FROM matches")]
    try:
        connection.execute("BEGIN IMMEDIATE")
        for match_id in match_ids:
            row = connection.execute(
                "SELECT summary_json FROM matches WHERE id = ?", (match_id,)
            ).fetchone()
            if row is None:
                raise ValueError(f"match id {match_id} does not exist")
            validate_totals(connection, match_id, json.loads(row[0]))
            rebuild_statistics(connection, match_id)
            validate_statistics(connection, match_id, json.loads(row[0]))
        connection.commit()
    except Exception:
        connection.rollback()
        raise
    finally:
        connection.close()
    return match_ids


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Rebuild derived Felt statistics from SQLite hand facts."
    )
    parser.add_argument("match_ids", nargs="*", type=int)
    parser.add_argument(
        "--database", type=Path, default=Path("data/felt.sqlite3")
    )
    arguments = parser.parse_args()
    try:
        rebuilt = rebuild(arguments.database, arguments.match_ids)
        print(f"rebuilt statistics for {len(rebuilt)} match(es): " + ", ".join(map(str, rebuilt)))
    except (OSError, ValueError, json.JSONDecodeError, sqlite3.Error) as error:
        parser.exit(1, f"rebuild_stats: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
