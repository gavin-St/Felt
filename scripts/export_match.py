#!/usr/bin/env python3
"""Export a Felt match's original summary and hand stream from SQLite."""

from __future__ import annotations

import argparse
import gzip
import json
import sqlite3
import zlib
from pathlib import Path


def export_match(
    database: Path, match_id: int, output: Path, compress: bool = False
) -> tuple[Path, Path, int]:
    connection = sqlite3.connect(database)
    try:
        row = connection.execute(
            "SELECT summary_json, hand_count FROM matches WHERE id = ?",
            (match_id,),
        ).fetchone()
        if row is None:
            raise ValueError(f"match id {match_id} does not exist")
        summary = json.loads(row[0])
        expected_hands = row[1]
        chunks = connection.execute(
            """SELECT chunk_index, first_hand_index, hand_count, codec, jsonl
               FROM hand_chunks WHERE match_id = ? ORDER BY chunk_index""",
            (match_id,),
        ).fetchall()
    finally:
        connection.close()

    output.mkdir(parents=True, exist_ok=True)
    summary_path = output / "summary.json"
    temporary_summary = output / ".summary.json.tmp"
    temporary_summary.write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8"
    )
    temporary_summary.replace(summary_path)

    hand_path = output / ("hands.jsonl.gz" if compress else "hands.jsonl")
    temporary_hands = output / (".hands.jsonl.gz.tmp" if compress else ".hands.jsonl.tmp")
    opener = gzip.open if compress else open
    hands_written = 0
    expected_chunk = 0
    with opener(temporary_hands, "wb") as destination:
        for chunk_index, first_hand, hand_count, codec, payload in chunks:
            if chunk_index != expected_chunk or first_hand != hands_written:
                raise ValueError("match history chunks are not contiguous")
            if codec != "zlib-jsonl":
                raise ValueError(f"unsupported history codec {codec!r}")
            raw = zlib.decompress(payload)
            lines = raw.splitlines(keepends=True)
            if len(lines) != hand_count or any(not line.endswith(b"\n") for line in lines):
                raise ValueError(f"invalid history chunk {chunk_index}")
            destination.write(raw)
            hands_written += hand_count
            expected_chunk += 1
    if hands_written != expected_hands:
        temporary_hands.unlink(missing_ok=True)
        raise ValueError(
            f"expected {expected_hands} hands but exported {hands_written}"
        )
    temporary_hands.replace(hand_path)
    return summary_path, hand_path, hands_written


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Export a Felt match from the local SQLite ledger."
    )
    parser.add_argument("match_id", type=int)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--database", type=Path, default=Path("data/felt.sqlite3")
    )
    parser.add_argument("--gzip", action="store_true")
    arguments = parser.parse_args()
    try:
        _, hand_path, hands = export_match(
            arguments.database,
            arguments.match_id,
            arguments.output,
            compress=arguments.gzip,
        )
        print(f"exported match_id={arguments.match_id} hands={hands} file={hand_path}")
    except (OSError, ValueError, json.JSONDecodeError, sqlite3.Error) as error:
        parser.exit(1, f"export_match: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
