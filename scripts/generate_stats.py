#!/usr/bin/env python3
"""Generate compact, verified match statistics from Felt JSON logs."""

from __future__ import annotations

import argparse
import gzip
import json
import math
import os
import shutil
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable


POSITIONS = ("button", "big_blind")
STREETS = ("preflop", "flop", "turn", "river")
RANKS = "23456789TJQKA"
SUITS = "cdsh"
ACTION_NAMES = {1: "fold", 2: "check", 3: "call", 4: "raise_to"}
VIOLATION_NAMES = {
    1: "nonzero_reserved",
    2: "unknown_action",
    3: "illegal_action",
    4: "invalid_raise_amount",
}


def checked_int(value: Any, name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{name} must be an integer")
    return value


def rounded(value: float) -> float:
    return round(value, 12)


def ratio(count: int, opportunities: int) -> dict[str, int | float]:
    rate = count / opportunities if opportunities else 0.0
    return {
        "count": count,
        "opportunities": opportunities,
        "rate": rounded(rate),
        "percentage": rounded(rate * 100.0),
    }


def chip_result(
    raw: int, adjusted: int, hands: int, big_blind: int
) -> dict[str, int | float]:
    scale = 100.0 / (big_blind * hands) if hands else 0.0
    return {
        "hands": hands,
        "raw_net_chips": raw,
        "adjusted_net_chips": adjusted,
        "raw_net_bb": rounded(raw / big_blind),
        "adjusted_net_bb": rounded(adjusted / big_blind),
        "raw_bb_per_100": rounded(raw * scale),
        "adjusted_bb_per_100": rounded(adjusted * scale),
    }


def card_text(card: int) -> str:
    if card < 0 or card >= 52:
        raise ValueError(f"invalid card value {card}")
    return RANKS[card >> 2] + SUITS[card & 3]


def ordered_hole(cards: list[int]) -> tuple[int, int]:
    if len(cards) != 2:
        raise ValueError("hole-card pair must contain two cards")
    first, second = cards
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


@dataclass
class RowStats:
    hands: int = 0
    raw_net: int = 0
    adjusted_net: int = 0
    wins: int = 0
    losses: int = 0
    chops: int = 0
    all_in_reached: int = 0

    def add(self, raw: int, adjusted: int, outcome: str, all_in: bool) -> None:
        self.hands += 1
        self.raw_net += raw
        self.adjusted_net += adjusted
        setattr(self, outcome, getattr(self, outcome) + 1)
        self.all_in_reached += int(all_in)

    def to_json(self, big_blind: int) -> dict[str, Any]:
        result = chip_result(self.raw_net, self.adjusted_net, self.hands, big_blind)
        result["raw_outcomes"] = {
            "wins": self.wins,
            "losses": self.losses,
            "chops": self.chops,
        }
        result["all_in_reached"] = ratio(self.all_in_reached, self.hands)
        return result

    def merge(self, other: "RowStats") -> None:
        self.hands += other.hands
        self.raw_net += other.raw_net
        self.adjusted_net += other.adjusted_net
        self.wins += other.wins
        self.losses += other.losses
        self.chops += other.chops
        self.all_in_reached += other.all_in_reached


@dataclass
class RunningVariance:
    count: int = 0
    mean: float = 0.0
    m2: float = 0.0

    def add(self, value: int) -> None:
        self.count += 1
        delta = value - self.mean
        self.mean += delta / self.count
        self.m2 += delta * (value - self.mean)

    def to_json(self, hands_per_sample: int, big_blind: int) -> dict[str, int | float]:
        variance = self.m2 / (self.count - 1) if self.count > 1 else 0.0
        standard_deviation = math.sqrt(variance)
        standard_error = (
            standard_deviation / math.sqrt(self.count) if self.count else 0.0
        )
        bb100_scale = 100.0 / (big_blind * hands_per_sample)
        return {
            "independent_samples": self.count,
            "hands_per_sample": hands_per_sample,
            "mean_chips_per_sample": rounded(self.mean),
            "standard_deviation_chips": rounded(standard_deviation),
            "standard_error_chips": rounded(standard_error),
            "standard_deviation_bb_per_100": rounded(
                standard_deviation * bb100_scale
            ),
            "standard_error_bb_per_100": rounded(
                standard_error * bb100_scale
            ),
        }


@dataclass
class BotStats:
    hands: int = 0
    raw_net: int = 0
    adjusted_net: int = 0
    position_hands: list[int] = field(default_factory=lambda: [0, 0])
    position_raw_net: list[int] = field(default_factory=lambda: [0, 0])
    position_adjusted_net: list[int] = field(default_factory=lambda: [0, 0])
    outcomes: dict[str, int] = field(
        default_factory=lambda: {"wins": 0, "losses": 0, "chops": 0}
    )
    vpip: int = 0
    pfr: int = 0
    saw_flop: int = 0
    showdowns: int = 0
    showdown_wins: int = 0
    cbet_opportunities: int = 0
    cbets: int = 0
    all_in_reached: list[int] = field(default_factory=lambda: [0, 0, 0, 0])
    all_in_initiated: list[int] = field(default_factory=lambda: [0, 0, 0, 0])
    exact_equity_sum: float = 0.0
    exact_equity_hands: int = 0
    showdown_raw_net: int = 0
    showdown_adjusted_net: int = 0
    nonshowdown_raw_net: int = 0
    nonshowdown_adjusted_net: int = 0
    pot_classes: dict[str, int] = field(
        default_factory=lambda: {
            "walk": 0,
            "limped_unraised": 0,
            "single_raised": 0,
            "three_bet": 0,
            "four_bet_plus": 0,
        }
    )
    actions: list[dict[str, int]] = field(
        default_factory=lambda: [
            {"fold": 0, "check": 0, "call": 0, "raise_to": 0}
            for _ in STREETS
        ]
    )
    cpu_times: list[int] = field(default_factory=list)
    wall_times: list[int] = field(default_factory=list)
    violations: dict[str, int] = field(default_factory=dict)
    buckets: dict[tuple[str, int], RowStats] = field(default_factory=dict)
    combos: dict[tuple[str, int], RowStats] = field(default_factory=dict)
    raw_variance: RunningVariance = field(default_factory=RunningVariance)
    adjusted_variance: RunningVariance = field(default_factory=RunningVariance)


def raw_outcomes(hand: dict[str, Any]) -> list[str]:
    result = hand["result"]
    if result["reason"] == 1:
        folded = checked_int(result["folded_position"], "folded_position")
        winner = 1 - folded
    else:
        ranks = result["showdown_rank"]
        if ranks[0] == ranks[1]:
            return ["chops", "chops"]
        winner = 0 if ranks[0] > ranks[1] else 1
    return ["wins" if position == winner else "losses" for position in range(2)]


def classify_pot(hand: dict[str, Any], saw_flop: bool) -> tuple[str, int, int | None]:
    raises = [
        decision
        for decision in hand["decisions"]
        if decision["street"] == 0 and decision["applied"]["type"] == 4
    ]
    count = len(raises)
    if count == 0:
        return ("limped_unraised" if saw_flop else "walk", count, None)
    if count == 1:
        pot_class = "single_raised"
    elif count == 2:
        pot_class = "three_bet"
    else:
        pot_class = "four_bet_plus"
    return pot_class, count, checked_int(raises[-1]["position"], "raise position")


def find_all_in_initiator(hand: dict[str, Any]) -> tuple[int, int] | None:
    for decision in hand["decisions"]:
        applied = decision["applied"]
        if applied["type"] == 4 and applied["amount_to"] == (
            decision["my_street_contribution"] + decision["my_stack"]
        ):
            return (
                checked_int(decision["position"], "all-in position"),
                checked_int(decision["street"], "all-in street"),
            )
    return None


def cbet_result(hand: dict[str, Any], aggressor: int | None) -> tuple[bool, bool]:
    if aggressor is None:
        return False, False
    flop_decisions = [decision for decision in hand["decisions"] if decision["street"] == 1]
    for decision in flop_decisions:
        if decision["position"] == aggressor:
            return True, decision["applied"]["type"] == 4
        if decision["applied"]["type"] != 2:
            return False, False
    return False, False


def percentile_99(values: list[int]) -> int:
    if not values:
        return 0
    ordered = sorted(values)
    return ordered[math.ceil(0.99 * len(ordered)) - 1]


def timing_json(cpu: list[int], wall: list[int]) -> dict[str, Any]:
    decisions = len(cpu)
    return {
        "decisions": decisions,
        "cpu_time_ns": {
            "mean": sum(cpu) / decisions if decisions else 0.0,
            "p99": percentile_99(cpu),
            "maximum": max(cpu, default=0),
        },
        "wall_time_ns": {
            "mean": sum(wall) / decisions if decisions else 0.0,
            "p99": percentile_99(wall),
            "maximum": max(wall, default=0),
        },
    }


def street_rates(values: list[int], hands: int) -> dict[str, Any]:
    return {street: ratio(values[index], hands) for index, street in enumerate(STREETS)}


def row_list(rows: dict[tuple[str, int], RowStats], big_blind: int) -> list[dict[str, Any]]:
    output = []
    for (label, position), stats in sorted(rows.items()):
        row = {"label": label, "position": POSITIONS[position]}
        row.update(stats.to_json(big_blind))
        output.append(row)
    return output


def aggregate_row_list(
    rows: dict[tuple[str, int], RowStats], big_blind: int
) -> list[dict[str, Any]]:
    aggregated: dict[str, RowStats] = {}
    for (label, _), stats in rows.items():
        aggregated.setdefault(label, RowStats()).merge(stats)
    output = []
    for label, stats in sorted(aggregated.items()):
        row = {"label": label}
        row.update(stats.to_json(big_blind))
        output.append(row)
    return output


def bot_json(
    bot: BotStats,
    artifact: dict[str, Any],
    big_blind: int,
    hands_per_sample: int,
) -> dict[str, Any]:
    positions = {}
    for position, name in enumerate(POSITIONS):
        positions[name] = chip_result(
            bot.position_raw_net[position],
            bot.position_adjusted_net[position],
            bot.position_hands[position],
            big_blind,
        )
    action_output = {}
    for street_index, street in enumerate(STREETS):
        counts = bot.actions[street_index]
        decisions = sum(counts.values())
        action_output[street] = {
            "decisions": decisions,
            **{
                name: ratio(count, decisions)
                for name, count in counts.items()
            },
        }
    total_all_in = sum(bot.all_in_reached)
    total_initiated = sum(bot.all_in_initiated)
    exact_equity = (
        bot.exact_equity_sum / bot.exact_equity_hands
        if bot.exact_equity_hands
        else 0.0
    )
    violation_total = sum(bot.violations.values())
    return {
        "index": artifact["index"],
        "name": artifact["name"],
        "sha256": artifact["sha256"],
        "results": {
            **chip_result(bot.raw_net, bot.adjusted_net, bot.hands, big_blind),
            "raw_outcomes": bot.outcomes,
            "showdown": {
                "raw_net_chips": bot.showdown_raw_net,
                "adjusted_net_chips": bot.showdown_adjusted_net,
            },
            "nonshowdown": {
                "raw_net_chips": bot.nonshowdown_raw_net,
                "adjusted_net_chips": bot.nonshowdown_adjusted_net,
            },
        },
        "positions": positions,
        "rates": {
            "vpip": ratio(bot.vpip, bot.hands),
            "pfr": ratio(bot.pfr, bot.hands),
            "raw_win": ratio(bot.outcomes["wins"], bot.hands),
            "showdown": ratio(bot.showdowns, bot.hands),
            "wtsd": ratio(bot.showdowns, bot.saw_flop),
            "w_sd": ratio(bot.showdown_wins, bot.showdowns),
            "cbet": ratio(bot.cbets, bot.cbet_opportunities),
        },
        "all_in": {
            "reached": ratio(total_all_in, bot.hands),
            "initiated": ratio(total_initiated, bot.hands),
            "reached_by_street": street_rates(
                bot.all_in_reached, bot.hands
            ),
            "initiated_by_street": street_rates(
                bot.all_in_initiated, bot.hands
            ),
            "average_exact_equity": {
                "hands": bot.exact_equity_hands,
                "rate": rounded(exact_equity),
                "percentage": rounded(exact_equity * 100.0),
            },
        },
        "pot_class": {
            name: ratio(count, bot.hands) for name, count in bot.pot_classes.items()
        },
        "actions": action_output,
        "timing": timing_json(bot.cpu_times, bot.wall_times),
        "violations": {
            "total": violation_total,
            "percentage_of_decisions": (
                violation_total * 100.0 / len(bot.cpu_times)
                if bot.cpu_times
                else 0.0
            ),
            "by_type": bot.violations,
        },
        "variance": {
            "raw": bot.raw_variance.to_json(hands_per_sample, big_blind),
            "adjusted": bot.adjusted_variance.to_json(hands_per_sample, big_blind),
        },
        "starting_hands": aggregate_row_list(bot.buckets, big_blind),
        "starting_hands_by_position": row_list(bot.buckets, big_blind),
        "exact_combinations_by_position": row_list(bot.combos, big_blind),
    }


def load_summary(directory: Path) -> dict[str, Any]:
    with (directory / "summary.json").open(encoding="utf-8") as source:
        summary = json.load(source)
    if summary.get("status") != "complete":
        raise ValueError(f"{directory}: match summary is not complete")
    if summary.get("result") is None:
        raise ValueError(f"{directory}: complete summary has no result")
    if len(summary.get("bots", [])) != 2:
        raise ValueError(f"{directory}: summary must contain two bots")
    return summary


def hand_log_path(directory: Path) -> Path:
    plain = directory / "hands.jsonl"
    compressed = directory / "hands.jsonl.gz"
    if plain.is_file():
        return plain
    if compressed.is_file():
        return compressed
    raise ValueError(f"{directory}: no hands.jsonl or hands.jsonl.gz")


def open_hand_log(path: Path):
    if path.suffix == ".gz":
        return gzip.open(path, "rt", encoding="utf-8")
    return path.open(encoding="utf-8")


def compress_hand_log(path: Path) -> Path:
    if path.suffix == ".gz":
        return path
    compressed = path.with_name(path.name + ".gz")
    temporary = path.with_name(".hands.jsonl.gz.tmp")
    try:
        with path.open("rb") as source, temporary.open("wb") as raw_output:
            with gzip.GzipFile(
                filename="", mode="wb", fileobj=raw_output, compresslevel=6, mtime=0
            ) as destination:
                shutil.copyfileobj(source, destination, length=1024 * 1024)
        os.replace(temporary, compressed)
        path.unlink()
    finally:
        if temporary.exists():
            temporary.unlink()
    return compressed


def generate(directory: Path, compress: bool = True) -> Path:
    summary = load_summary(directory)
    config = summary["config"]
    expected_hands = checked_int(config["hand_count"], "hand_count")
    starting_stack = checked_int(config["starting_stack"], "starting_stack")
    big_blind = checked_int(config["big_blind"], "big_blind")
    duplicate = bool(config["duplicate"])
    bots = [BotStats(), BotStats()]
    pair_raw = [0, 0]
    pair_adjusted = [0, 0]
    hands_read = 0
    source_path = hand_log_path(directory)

    with open_hand_log(source_path) as source:
        for line_number, line in enumerate(source, 1):
            if not line.strip():
                raise ValueError(f"{directory}: empty JSONL line {line_number}")
            hand = json.loads(line)
            if hand["hand_index"] != hands_read:
                raise ValueError(f"{directory}: noncontiguous hand index at line {line_number}")
            mapping = hand["bot_by_position"]
            if sorted(mapping) != [0, 1]:
                raise ValueError(f"{directory}: invalid bot mapping at line {line_number}")
            result = hand["result"]
            raw_net = result["raw_net"]
            adjusted_net = result["adjusted_net"]
            if sum(raw_net) != 0 or sum(adjusted_net) != 0:
                raise ValueError(f"{directory}: non-zero-sum hand at line {line_number}")

            outcomes = raw_outcomes(hand)
            showdown = result["reason"] == 2
            saw_flop = showdown or result["ending_street"] >= 1
            reached = showdown and result["committed"] == [starting_stack, starting_stack]
            all_in_street = result["ending_street"] if reached else None
            initiator = find_all_in_initiator(hand)
            pot_class, _, aggressor = classify_pot(hand, saw_flop)
            cbet_opportunity, cbet_made = cbet_result(hand, aggressor)

            for position in range(2):
                bot_index = checked_int(mapping[position], "bot index")
                bot = bots[bot_index]
                raw = checked_int(raw_net[position], "raw net")
                adjusted = checked_int(adjusted_net[position], "adjusted net")
                bot.hands += 1
                bot.raw_net += raw
                bot.adjusted_net += adjusted
                bot.position_hands[position] += 1
                bot.position_raw_net[position] += raw
                bot.position_adjusted_net[position] += adjusted
                bot.outcomes[outcomes[position]] += 1
                bot.saw_flop += int(saw_flop)
                bot.showdowns += int(showdown)
                bot.showdown_wins += int(showdown and outcomes[position] == "wins")
                if showdown:
                    bot.showdown_raw_net += raw
                    bot.showdown_adjusted_net += adjusted
                else:
                    bot.nonshowdown_raw_net += raw
                    bot.nonshowdown_adjusted_net += adjusted
                bot.pot_classes[pot_class] += 1
                if reached:
                    bot.all_in_reached[all_in_street] += 1
                if initiator is not None and initiator[0] == position:
                    bot.all_in_initiated[initiator[1]] += 1
                if cbet_opportunity and aggressor == position:
                    bot.cbet_opportunities += 1
                    bot.cbets += int(cbet_made)

                equity = result.get("equity")
                if equity is not None:
                    boards = checked_int(equity["boards"], "equity boards")
                    wins = checked_int(equity["wins"][position], "equity wins")
                    ties = checked_int(equity["ties"], "equity ties")
                    bot.exact_equity_sum += (2 * wins + ties) / (2 * boards)
                    bot.exact_equity_hands += 1

                label = bucket_label(hand["hole_cards"][position])
                combo = combo_label(hand["hole_cards"][position])
                bot.buckets.setdefault((label, position), RowStats()).add(
                    raw, adjusted, outcomes[position], reached
                )
                bot.combos.setdefault((combo, position), RowStats()).add(
                    raw, adjusted, outcomes[position], reached
                )
                pair_raw[bot_index] += raw
                pair_adjusted[bot_index] += adjusted

            preflop_by_position = [[], []]
            for decision in hand["decisions"]:
                position = checked_int(decision["position"], "decision position")
                bot = bots[mapping[position]]
                street = checked_int(decision["street"], "decision street")
                if street < 0 or street >= len(STREETS):
                    raise ValueError(
                        f"{directory}: invalid street at line {line_number}"
                    )
                action = checked_int(decision["applied"]["type"], "action type")
                action_name = ACTION_NAMES.get(action, f"unknown_{action}")
                if action_name not in bot.actions[street]:
                    bot.actions[street][action_name] = 0
                bot.actions[street][action_name] += 1
                bot.cpu_times.append(checked_int(decision["cpu_time_ns"], "CPU time"))
                bot.wall_times.append(
                    checked_int(decision["wall_time_ns"], "wall time")
                )
                violation = checked_int(decision["violation"], "violation")
                if violation:
                    name = VIOLATION_NAMES.get(violation, f"code_{violation}")
                    bot.violations[name] = bot.violations.get(name, 0) + 1
                if street == 0:
                    preflop_by_position[position].append(action)

            for position in range(2):
                bot = bots[mapping[position]]
                bot.vpip += int(
                    any(action in (3, 4) for action in preflop_by_position[position])
                )
                bot.pfr += int(
                    any(
                        action_type == 4
                        for action_type in preflop_by_position[position]
                    )
                )

            hands_read += 1
            sample_complete = not duplicate or (hands_read % 2 == 0)
            if sample_complete:
                for bot_index, bot in enumerate(bots):
                    bot.raw_variance.add(pair_raw[bot_index])
                    bot.adjusted_variance.add(pair_adjusted[bot_index])
                pair_raw = [0, 0]
                pair_adjusted = [0, 0]

    if hands_read != expected_hands:
        raise ValueError(
            f"{directory}: expected {expected_hands} hands, read {hands_read}"
        )
    if duplicate and hands_read % 2:
        raise ValueError(f"{directory}: duplicate log ended mid-pair")

    result = summary["result"]
    for bot_index, bot in enumerate(bots):
        if bot.raw_net != result["raw_net_by_bot"][bot_index]:
            raise ValueError(f"{directory}: raw bot total does not match summary")
        if bot.adjusted_net != result["adjusted_net_by_bot"][bot_index]:
            raise ValueError(f"{directory}: adjusted bot total does not match summary")
        if bot.position_raw_net != result["raw_net_by_bot_and_position"][bot_index]:
            raise ValueError(f"{directory}: raw position totals do not match summary")
        if (
            bot.position_adjusted_net
            != result["adjusted_net_by_bot_and_position"][bot_index]
        ):
            raise ValueError(
                f"{directory}: adjusted position totals do not match summary"
            )
        if sum(bot.outcomes.values()) != bot.hands:
            raise ValueError(f"{directory}: raw outcomes do not cross-foot")
        if sum(row.hands for row in bot.buckets.values()) != bot.hands:
            raise ValueError(f"{directory}: starting-hand rows do not cross-foot")
        if sum(row.hands for row in bot.combos.values()) != bot.hands:
            raise ValueError(f"{directory}: exact-combo rows do not cross-foot")

    hands_per_sample = 2 if duplicate else 1
    final_hands_name = (
        "hands.jsonl.gz" if compress or source_path.suffix == ".gz" else source_path.name
    )
    output = {
        "schema_version": 1,
        "source": {
            "summary_schema_version": summary["schema_version"],
            "summary_file": "summary.json",
            "hands_file": final_hands_name,
            "hand_count": hands_read,
        },
        "match": {
            "hands": hands_read,
            "duplicate": duplicate,
            "independent_samples": hands_read // hands_per_sample,
            "big_blind": big_blind,
        },
        "bots": [
            bot_json(bot, summary["bots"][index], big_blind, hands_per_sample)
            for index, bot in enumerate(bots)
        ],
    }
    output_path = directory / "stats.json"
    temporary = directory / ".stats.json.tmp"
    with temporary.open("w", encoding="utf-8") as destination:
        json.dump(output, destination, indent=2, allow_nan=False)
        destination.write("\n")
    os.replace(temporary, output_path)
    if compress:
        compress_hand_log(source_path)
    return output_path


def discover(paths: Iterable[Path]) -> list[Path]:
    found: set[Path] = set()
    for path in paths:
        if (path / "summary.json").is_file() and (
            (path / "hands.jsonl").is_file()
            or (path / "hands.jsonl.gz").is_file()
        ):
            found.add(path.resolve())
            continue
        if not path.is_dir():
            raise ValueError(f"{path}: not a match directory or directory tree")
        for summary in path.rglob("summary.json"):
            directory = summary.parent
            if (directory / "hands.jsonl").is_file() or (
                directory / "hands.jsonl.gz"
            ).is_file():
                found.add(directory.resolve())
    return sorted(found)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate verified stats.json files from Felt match logs."
    )
    parser.add_argument(
        "paths",
        nargs="*",
        type=Path,
        default=[Path("results")],
        help="match directories or trees to scan (default: results)",
    )
    parser.add_argument(
        "--keep-jsonl",
        action="store_true",
        help="leave a newly generated plain hands.jsonl uncompressed",
    )
    arguments = parser.parse_args()
    try:
        directories = discover(arguments.paths)
        if not directories:
            raise ValueError("no complete-looking Felt match directories found")
        for directory in directories:
            output = generate(directory, compress=not arguments.keep_jsonl)
            print(f"generated {output}")
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        parser.exit(1, f"generate_stats: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
