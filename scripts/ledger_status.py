#!/usr/bin/env python3
"""Report Felt ledger storage and estimate planned manual imports."""

from __future__ import annotations

import argparse
import json
import sqlite3
from pathlib import Path


DEFAULT_BYTES_PER_HAND = 800.0
DEFAULT_BUDGET_GB = 10.0


def file_bytes(database: Path) -> int:
    return sum(
        path.stat().st_size
        for path in (database, Path(str(database) + "-wal"), Path(str(database) + "-shm"))
        if path.exists()
    )


def status(
    database: Path,
    additional_matches: int,
    hands_per_match: int,
    budget_gb: float,
) -> dict[str, object]:
    if additional_matches < 0 or hands_per_match <= 0 or budget_gb <= 0.0:
        raise ValueError("matches must be nonnegative; hands and budget must be positive")
    current_bytes = file_bytes(database)
    stored_hands = 0
    stored_matches = 0
    if database.exists():
        connection = sqlite3.connect(f"file:{database.resolve()}?mode=ro", uri=True)
        try:
            stored_matches, stored_hands = connection.execute(
                "SELECT COUNT(*), COALESCE(SUM(hand_count), 0) FROM matches"
            ).fetchone()
        finally:
            connection.close()
    bytes_per_hand = (
        current_bytes / stored_hands if stored_hands >= 1000 else DEFAULT_BYTES_PER_HAND
    )
    additional_hands = additional_matches * hands_per_match
    projected_bytes = current_bytes + round(additional_hands * bytes_per_hand)
    budget_bytes = round(budget_gb * 1_000_000_000)
    return {
        "database": str(database),
        "stored_matches": stored_matches,
        "stored_hands": stored_hands,
        "current_bytes": current_bytes,
        "estimated_bytes_per_hand": bytes_per_hand,
        "additional_matches": additional_matches,
        "hands_per_match": hands_per_match,
        "additional_hands": additional_hands,
        "projected_bytes": projected_bytes,
        "budget_bytes": budget_bytes,
        "over_budget": projected_bytes > budget_bytes,
    }


def gibibytes(byte_count: int) -> float:
    return byte_count / (1024.0 ** 3)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Report ledger size and estimate additional match storage."
    )
    parser.add_argument("--database", type=Path, default=Path("data/felt.sqlite3"))
    parser.add_argument("--additional-matches", type=int, default=0)
    parser.add_argument("--hands-per-match", type=int, default=20000)
    parser.add_argument("--budget-gb", type=float, default=DEFAULT_BUDGET_GB)
    parser.add_argument("--json", action="store_true")
    arguments = parser.parse_args()
    try:
        result = status(
            arguments.database,
            arguments.additional_matches,
            arguments.hands_per_match,
            arguments.budget_gb,
        )
    except (OSError, ValueError, sqlite3.Error) as error:
        parser.exit(1, f"ledger_status: {error}\n")
    if arguments.json:
        print(json.dumps(result, sort_keys=True, separators=(",", ":")))
        return 0
    print(
        f"ledger: {result['stored_matches']} matches, {result['stored_hands']} hands, "
        f"{gibibytes(result['current_bytes']):.3f} GiB"
    )
    print(f"observed storage: {result['estimated_bytes_per_hand']:.1f} bytes/hand")
    if result["additional_matches"]:
        print(
            f"projected after {result['additional_matches']} more matches: "
            f"{gibibytes(result['projected_bytes']):.3f} GiB"
        )
    if result["over_budget"]:
        print(
            f"WARNING: projection exceeds the {arguments.budget_gb:g} GB local "
            "budget; no data was pruned."
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
