#!/usr/bin/env python3
"""Check the numbers on the bot pages against the bots they describe.

The tables under "The numbers" on a bot page are hand-written, and the code
they describe is not. Nothing stops the two drifting apart except this, which
re-derives the tables from the C sources and fails on the first figure that no
longer matches. Tune a threshold without updating the page and the build says
so, naming both values.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parent.parent
BOTS = REPOSITORY / "bots"
PROFILES = REPOSITORY / "web" / "data" / "bots.json"

failures: list[str] = []


def fail(message: str) -> None:
    failures.append(message)


def source(name: str) -> str:
    return (BOTS / name).read_text(encoding="utf-8")


def defines(text: str) -> dict[str, str]:
    found = {}
    for match in re.finditer(r"^#define\s+(\w+)\s+(.+)$", text, re.M):
        value = re.sub(r"/\*.*?\*/", "", match.group(2)).strip()
        found[match.group(1)] = value
    return found


def body(text: str, name: str) -> str:
    """A top-level function body, which ends at a brace in column one."""
    start = text.index(f"{name}(")
    end = text.index("\n}\n", start)
    return text[start:end]


def table(profile: dict, title: str) -> dict:
    for entry in profile.get("tables", []):
        if entry["title"] == title:
            return entry
    raise AssertionError(f"no table titled {title!r}")


def published(profile: dict, title: str, column: int = 1) -> dict[str, str]:
    entry = table(profile, title)
    return {row[0]: row[column] for row in entry["rows"]}


def check(label: str, expected: object, actual: object) -> None:
    if str(expected) != str(actual):
        fail(f"{label}: the page says {actual!r}, the code says {expected!r}")


def main() -> int:
    profiles = json.loads(PROFILES.read_text(encoding="utf-8"))
    board_value = source("board_value.c")
    range_read = source("range_read.c")
    bet_sizing = source("bet_sizing.c")
    raise_rules = source("raise_rules.c")
    call_rules = source("call_rules.c")
    slp_odds = source("slp_odds/slp_odds.c")

    for slug in ("slp-odds", "the-crusher"):
        if slug not in profiles:
            fail(f"{slug} has no profile")
            continue
        if not profiles[slug].get("tables"):
            fail(f"{slug} publishes no tables")

    if failures:
        return report()

    crusher = profiles["the-crusher"]
    odds = profiles["slp-odds"]

    # ---- hand value, shared by both bots -----------------------------------
    base = body(board_value, "base_points")
    wanted_bases = {
        "Quads or a straight flush": "100",
        "Full house": "95",
        "Flush": "90",
        "Straight": "86",
        "A set": "80",
        "Trips, using the board's pair": "74",
        "Two pair, both over the board": "68",
        "Two pair, both hole cards": "64",
        "Overpair": "54",
        "Two pair, middle": "54",
        "Two pair, under": "48",
        "Top pair": "44",
        "Middle pair": "26",
        "Two pair, both on the board": "20",
        "Bottom pair": "19",
        "Underpair": "17",
        "The board's pair, plus a kicker": "11",
        "High card": "4",
    }
    in_source = re.findall(r"return\s+(\d+);", base) + re.findall(
        r"return made->is_set \? (\d+) : (\d+);", base
    )
    flat = set()
    for item in in_source:
        flat.update(item if isinstance(item, tuple) else (item,))
    for name, points in wanted_bases.items():
        if points not in flat:
            fail(f"hand value {name!r} is published as {points}, which base_points never returns")
    for slug, profile in (("slp-odds", odds), ("the-crusher", crusher)):
        rows = published(profile, "Hand value, before the board")
        for name, points in wanted_bases.items():
            check(f"{slug} hand value, {name}", points, rows.get(name))

    penalties = sorted(int(value) for value in
                       re.findall(r"penalty \+= (\d+);", board_value))
    for slug, profile in (("slp-odds", odds), ("the-crusher", crusher)):
        rows = published(profile, "What the board takes back")
        listed = sorted(int(value) for value in rows.values())
        check(f"{slug} board penalties", penalties, listed)

    # ---- slp-odds bands ----------------------------------------------------
    bv = defines(board_value)
    bands = published(odds, "Bands, and what each one does")
    raise_bands = published(odds, "Bands, and what each one does", column=2)
    for band, key, bump in (
        ("Nutted", "NUTTED_POINTS", "FACING_RAISE_NUTTED_BUMP"),
        ("Strong", "STRONG_POINTS", "FACING_RAISE_STRONG_BUMP"),
    ):
        check(f"slp-odds {band} band", f"{bv[key]}+", bands[band])
        check(
            f"slp-odds {band} band facing a raise",
            f"{int(bv[key]) + int(bv[bump])}+",
            raise_bands[band],
        )
    check("slp-odds Medium band", f"{bv['MEDIUM_POINTS']}+", bands["Medium"])
    check("slp-odds Marginal band", f"{bv['MARGINAL_POINTS']}+", bands["Marginal"])

    so = defines(slp_odds)
    action = published(odds, "Bands, and what each one does", column=3)
    check(
        "slp-odds Medium price ceiling",
        f"Call at {so['MEDIUM_MAX_PRICE_PERCENT']}% of the pot or less",
        action["Medium"],
    )
    check(
        "slp-odds Marginal price ceiling",
        f"Call at {so['MARGINAL_MAX_PRICE_PERCENT']}% or less",
        action["Marginal"],
    )

    # ---- the opponent read -------------------------------------------------
    rr = defines(range_read)
    claims = published(crusher, "What their line claims")
    for label, key in (
        ("Raised three times", "CLAIM_RERAISED"),
        ("Raised our bet", "CLAIM_RAISED"),
        ("Bet", "CLAIM_BET"),
        ("Called", "CLAIM_CALLED"),
        ("Not yet acted", "CLAIM_NO_ACTION_YET"),
        ("Checked", "CLAIM_CHECKED"),
    ):
        check(f"crusher claim, {label}", rr[key], claims[label])
    check("crusher claim, raised twice", int(rr["CLAIM_RAISED"]) + 8, claims["Raised twice"])
    discount = re.search(r"if \(continuation_bet\) \{\s*score -= (\d+);", range_read)
    check(
        "crusher continuation bet",
        int(rr["CLAIM_BET"]) - int(discount.group(1)),
        claims["A continuation bet"],
    )

    ladder = [int(value) for value in
              re.findall(r"return (-?\d+); /\*", body(range_read, "preflop_pot_adjustment"))]
    moves = published(crusher, "And what moves it")
    for label, value in (
        ("Limped pot", ladder[0]),
        ("A single open", ladder[1]),
        ("Three-bet pot", ladder[2]),
        ("Four-bet pot", ladder[3]),
        ("Five-bet pot or more", ladder[4]),
    ):
        check(f"crusher preflop ladder, {label}",
              f"{value:+d}".replace("+", "+") if value > 0 else str(value),
              moves[label])

    # ---- sizing ------------------------------------------------------------
    pairs = re.findall(
        r"\*small = ([\d.]+); \*large = ([\d.]+); \*weight_large = (\d+);", bet_sizing
    )
    in_code = {(float(a), float(b), int(w)) for a, b, w in pairs}
    sizing = table(crusher, "Two sizes for everything")
    on_page = set()
    for row in sizing["rows"]:
        small = float(row[1].replace("x pot", "").replace("x", ""))
        large = float(row[2].replace("x pot", "").replace("x", ""))
        on_page.add((small, large, int(row[3].rstrip("%"))))
    if on_page != in_code:
        fail(
            "the sizing table does not match bet_sizing.c\n"
            f"    only on the page: {sorted(on_page - in_code)}\n"
            f"    only in the code: {sorted(in_code - on_page)}"
        )

    weights = published(crusher, "What moves the odds")
    for label, pattern in (
        ("Live draws on the board", r"weight \+= (12);"),
        ("Bone-dry board", r"weight -= (12);"),
        ("Paired board", r"weight -= (8);"),
        ("Bluffing with eight outs or more", r"weight \+= (10);"),
        ("Bluffing into a range that has shown nothing", r"weight -= (10);"),
        ("Value betting into a range that has shown nothing", r"weight \+= (6);"),
    ):
        found = re.search(pattern, bet_sizing)
        if found is None:
            fail(f"crusher sizing weight {label!r} is on the page but not in bet_sizing.c")
            continue
        sign = "+" if "+=" in pattern else "-"
        check(f"crusher sizing weight, {label}", f"{sign}{found.group(1)}", weights[label])

    # ---- thresholds --------------------------------------------------------
    edges = {**defines(raise_rules), **defines(call_rules)}
    thresholds = published(crusher, "When to raise, when to call")
    check("crusher value bet edge",
          f"+{edges['EDGE_BET_FOR_STACKS']} or more, unbet pot",
          next(key for key in thresholds if key.startswith("+22")))
    check("crusher value raise edge",
          f"+{edges['EDGE_RAISE']} or more, facing a bet",
          next(key for key in thresholds if key.startswith("+20")))
    ceiling = re.search(r"int ceiling = (\d+) - range_score / (\d+);", call_rules)
    note = table(crusher, "When to raise, when to call")["note"]
    if f"{ceiling.group(1)} minus a third" not in note:
        fail(
            "the bluff-catch note does not describe "
            f"{ceiling.group(1)} - range_score / {ceiling.group(2)}"
        )

    # ---- bluff frequencies -------------------------------------------------
    bluff = body(raise_rules, "felt_bluff_raise")
    cuts = re.findall(r"read->score < (\d+)\) return roll % UINT64_C\((\d+)\)", bluff)
    words = {"3": "One time in three", "5": "One time in five", "8": "One time in eight"}
    frequencies = published(crusher, "Frequencies")
    for cut, divisor in cuts:
        label = f"Their range scores under {cut}" if cut == "35" else f"Under {cut}"
        check(f"crusher bluff frequency under {cut}", words[divisor], frequencies[label])
    tail = re.findall(r"return roll % UINT64_C\((\d+)\) == 0U;", bluff)[-1]
    check("crusher bluff frequency, strong range",
          words[tail], frequencies[f"{cuts[-1][0]} or more"])

    return report()


def report() -> int:
    if failures:
        print("published numbers have drifted from the code:", file=sys.stderr)
        for message in failures:
            print(f"  - {message}", file=sys.stderr)
        return 1
    print("bot page tables match the bots they describe")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
