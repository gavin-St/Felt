#!/usr/bin/env python3
"""Keep the slp-odds and crusher pages aligned with their policy tables."""

from __future__ import annotations

import json
import sys
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parent.parent
BOTS = REPOSITORY / "bots"
PROFILES = REPOSITORY / "web" / "data" / "bots.json"


def table(profile: dict, title: str) -> dict:
    for entry in profile.get("tables", []):
        if entry["title"] == title:
            return entry
    raise AssertionError(f"no table titled {title!r}")


def rows(profile: dict, title: str) -> dict[str, list[str]]:
    return {row[0]: row[1:] for row in table(profile, title)["rows"]}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def check_shared_tables(profile: dict, slug: str) -> None:
    hands = rows(profile, "Hand value, before the board")
    expected_hands = {
        "Quads or a straight flush": "100",
        "Full house": "95",
        "Flush": "90",
        "Straight": "86",
        "A set": "80",
        "Trips, using the board's pair": "74; 68 with weak kicker",
        "Two pair, both hole cards": "64",
        "Overpair": "54",
        "Top pair": "40-48 by kicker",
        "Middle pair": "26",
        "Bottom pair": "19",
        "Underpair": "17",
        "The board's pair, plus a kicker": "11",
        "High card": "4",
    }
    for label, value in expected_hands.items():
        require(hands.get(label) == [value], f"{slug}: {label} is not {value}")

    draws = rows(profile, "Draw value")
    expected_draws = {
        "Combo draw": "48",
        "Flush draw": "32",
        "Open-ended or double-gutshot": "28",
        "Gutshot": "14",
        "Overcards plus backdoor flush": "10",
    }
    for label, value in expected_draws.items():
        require(draws.get(label) == [value], f"{slug}: {label} is not {value}")

    penalties = rows(profile, "What the board takes back")
    expected_penalties = {
        "Flush on the board": "34",
        "Four to a flush": "20",
        "Three of a suit": "7",
        "Straight on the board": "30",
        "Four to a straight": "13",
        "Three inside a five-rank window": "4",
        "Quads on the board": "40",
        "Trips on the board": "18",
        "A pair on the board": "9",
    }
    for label, value in expected_penalties.items():
        require(
            penalties.get(label, [None])[0] == value,
            f"{slug}: {label} penalty is not {value}",
        )


def check_crusher_tables(profile: dict) -> None:
    sizes = rows(profile, "Two sizes for everything")
    expected_sizes = {
        "Bet vs merged": ["0.66x pot", "1.25x pot", "45%"],
        "Bet vs polarised": ["0.33x pot", "0.66x pot", "30%"],
        "Thin bet vs merged": ["0.5x pot", "0.5x pot", "single"],
        "Thin bet vs polarised": ["0.33x pot", "0.33x pot", "single"],
        "Raise vs merged": ["3x", "4.5x", "45%"],
        "Raise vs polarised": ["3x", "3.5x", "30%"],
        "Re-raise, either": ["2.5x", "3x", "50%"],
    }
    require(sizes == expected_sizes, "crusher sizing table has drifted")

    moves = rows(profile, "What moves the odds")
    expected_moves = {
        "Live draws on the board": ["+12"],
        "Bone-dry board": ["-12"],
        "Paired board": ["-8"],
        "Our hand has eight outs or more": ["+10"],
        "Opponent range has shown nothing": ["+8"],
        "They called a bet and did not raise one": ["+10"],
        "The size that would get the stacks in is nearer": ["+/-15"],
    }
    require(moves == expected_moves, "crusher sizing adjustments have drifted")

    catches = rows(profile, "Bluff-catch frequencies")
    require(catches["<= 0.33 pot"] == ["100 / 100", "80 / 90"],
            "small-bet bluff-catch row drifted")
    require(catches["> 1.50"] == ["20 / 40", "5 / 15"],
            "overbet bluff-catch row drifted")

    pricing = rows(profile, "Outs a price is asking for")
    require(pricing == {
        "Half the pot, 25%": ["6", "12"],
        "The pot, 33%": ["9", "16"],
        "Twice the pot, 40%": ["11", "19"],
    }, "crusher draw-pricing table has drifted")

    barrels = rows(profile, "What their line claims")
    for label, value in (("A continuation bet", "22"),
                         ("Their second barrel", "28"),
                         ("Their third barrel", "30")):
        require(barrels[label] == [value],
                f"barrel claim {label} is not {value}")


def check_source_contracts() -> None:
    board = (BOTS / "board_value.c").read_text(encoding="utf-8")
    range_read = (BOTS / "range_read.c").read_text(encoding="utf-8")
    raises = (BOTS / "raise_rules.c").read_text(encoding="utf-8")
    calls = (BOTS / "call_rules.c").read_text(encoding="utf-8")
    sizing = (BOTS / "bet_sizing.c").read_text(encoding="utf-8")

    for fragment in (
        "return 40 + kicker_points",
        "return *kicker == FELT_KICKER_STRONG ? 74 : 68",
        "#define BOARD_HAND_PLAYS_BOARD 6",
        "#define BOARD_HAND_BASE 8",
        "BOARD_HAND_BASE + 3 * kicker_points(rank)",
        "made->category == FELT_MADE_TRIPS && texture->trips_on_board",
        "ours > profile.pair_rank ? 64 : 48",
        "if (made->is_set) return 95;",
        "return 82;",
        "texture->pair_count >= 2U",
        "value.made_points > value.draw_points",
        "else if (texture->max_suit_count >= 4)",
        "else if (texture->max_cards_in_five_rank_window >= 4)",
    ):
        require(fragment in board, f"shared scoring contract missing {fragment!r}")

    for fragment in (
        "0.5 * ((double)read->score - 50.0)",
        "#define CLAIM_CONTINUATION_BET 22",
        "#define CLAIM_SECOND_BARREL 28",
        "#define CLAIM_THIRD_BARREL 30",
        "#define SMALL_OPEN_SCORE 45",
        "score += 3 * (int)read.calls_of_our_bets",
        "read.polarisation -= 10 * (int)read.calls_of_our_bets",
        "read.polarisation * (100 - read.score)",
        "500 + (6000 * read.air_share_basis_points) / 10000",
        "they_check_raised",
    ):
        require(fragment in range_read, f"range contract missing {fragment!r}")

    for fragment in (
        "#define DELTA_BET_VALUE 22",
        "#define DELTA_RAISE_MERGED 20",
        "#define DELTA_RAISE_POLARISED 28",
        "#define BARREL_SHIFT_SECOND 6",
        "#define BARREL_SHIFT_THIRD 12",
        "#define RAISE_SHIFT 8",
        "polarised ? 25 : 50",
        "polarised ? 0 : 10",
    ):
        require(fragment in raises, f"raise contract missing {fragment!r}")

    for fragment in (
        "{100, 85, 65, 40, 20}",
        "{80, 55, 35, 15, 5}",
        "static const int on_flop[3] = {60, 90, 110}",
        "static const int on_turn[3] = {120, 160, 190}",
        "delta < (double)(DELTA_CALL + shift)",
        "delta < (double)(-10 + shift)",
        "state->street == FELT_STREET_RIVER",
        "read->bluff_rate_basis_points - 2000",
    ):
        require(fragment in calls, f"call contract missing {fragment!r}")

    for fragment in (
        "*small = 0.66; *large = 1.25; *weight_large = 45",
        "*small = 0.33; *large = 0.66; *weight_large = 30",
        "*small = 2.5; *large = 3.0; *weight_large = 50",
        "if (weight < 10) weight = 10",
        "if (weight > 90) weight = 90",
    ):
        require(fragment in sizing, f"sizing contract missing {fragment!r}")


def main() -> int:
    try:
        profiles = json.loads(PROFILES.read_text(encoding="utf-8"))
        for slug in ("slp-odds", "the-crusher"):
            check_shared_tables(profiles[slug], slug)
        check_crusher_tables(profiles["the-crusher"])
        check_source_contracts()
    except (AssertionError, KeyError, OSError, json.JSONDecodeError) as error:
        print(f"published numbers have drifted from the code: {error}", file=sys.stderr)
        return 1
    print("bot page tables match the bots they describe")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
