#!/usr/bin/env python3
"""Build uncertainty-aware, Elo-scale ratings from Felt match results."""

from __future__ import annotations

import argparse
import math
import sqlite3
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from finalize_match import initialize_database  # noqa: E402


RATING_VERSION = 2
DEFAULT_MARGIN_SCALE = 1.0
# The rating axis is arbitrary and these constants set its width.
#
# The fit divides each bot's won matches by the size of the field, so on its own
# a bot that beats everyone lands near +BASE * ELO_PER_LOGIT whether it beat ten
# opponents or a hundred: adding bots fills the axis in rather than stretching
# it. Beating twenty is a bigger claim than beating three, though, so the fitted
# ratings are stretched afterwards by a factor that grows with the field.
#
# That factor used to be a square root, which kept opening the axis faster than
# the results justified -- twenty-nine bots put the top of the field at 2356,
# and every bot added pushed it further with no sign of settling. It is now a
# logarithm, which is both lower and flattening: each new bot widens the axis
# by less than the one before, which is the right shape, because the tenth
# opponent tells you much more about a bot than the thirtieth.
#
# The two constants are round on purpose. Solving them to put the top of a
# twenty-nine bot field on 2200 exactly would be a false precision: the target
# was a range someone picked by eye, and a curve fitted to four decimal places
# would suggest it came from somewhere firmer than that. A gain of 0.45 and a
# shift of 4 land near enough -- about 2010 at fourteen bots and 2210 at
# twenty-nine -- and can be moved by hand without re-solving anything.
#
# Fifty bots reach about 2370 rather than the 2620 the square root wanted, and
# two bots still separate a little instead of collapsing onto 1500.
#
# The standard error is stretched by the same factor on purpose. Widening the
# axis without widening the error bars would not separate anything, it would
# only draw the same uncertainty smaller.
FIELD_LOG_SHIFT = 4.0
FIELD_LOG_GAIN = 0.45


def field_stretch(size: float) -> float:
    """How far the fitted ratings are opened out for a field of this size."""
    return FIELD_LOG_GAIN * math.log1p(size / FIELD_LOG_SHIFT)
ELO_PER_LOGIT = 400.0 / math.log(10.0)
BASE_WIN_LOGIT = 4.0
MARGIN_BONUS_LOGIT = 0.60
OUTCOME_STANDARD_ERROR_ELO = 400.0


@dataclass(frozen=True)
class Observation:
    match_id: int
    profile_id: int
    bot_a: int
    bot_b: int
    margin: float
    margin_standard_error: float


def direction_first_logit(margin: float, margin_scale: float) -> float:
    if margin == 0.0:
        return 0.0
    magnitude_bonus = MARGIN_BONUS_LOGIT * math.tanh(abs(margin) / margin_scale)
    return math.copysign(BASE_WIN_LOGIT + magnitude_bonus, margin)


def invert(matrix: list[list[float]]) -> list[list[float]]:
    size = len(matrix)
    augmented = [
        row[:] + [1.0 if column == index else 0.0 for column in range(size)]
        for index, row in enumerate(matrix)
    ]
    for column in range(size):
        pivot = max(range(column, size), key=lambda row: abs(augmented[row][column]))
        if abs(augmented[pivot][column]) < 1e-12:
            raise ValueError("rating graph produced a singular system")
        augmented[column], augmented[pivot] = augmented[pivot], augmented[column]
        divisor = augmented[column][column]
        augmented[column] = [value / divisor for value in augmented[column]]
        for row in range(size):
            if row == column:
                continue
            multiplier = augmented[row][column]
            if multiplier == 0.0:
                continue
            augmented[row] = [
                value - multiplier * pivot_value
                for value, pivot_value in zip(augmented[row], augmented[column])
            ]
    return [row[size:] for row in augmented]


def components(observations: list[Observation]) -> list[set[int]]:
    neighbors: dict[int, set[int]] = {}
    for observation in observations:
        neighbors.setdefault(observation.bot_a, set()).add(observation.bot_b)
        neighbors.setdefault(observation.bot_b, set()).add(observation.bot_a)
    found: list[set[int]] = []
    unseen = set(neighbors)
    while unseen:
        pending = [min(unseen)]
        member_set: set[int] = set()
        while pending:
            bot = pending.pop()
            if bot in member_set:
                continue
            member_set.add(bot)
            pending.extend(neighbors[bot] - member_set)
        unseen -= member_set
        found.append(member_set)
    return found


def fit_component(
    bot_ids: set[int], observations: list[Observation], margin_scale: float
) -> dict[int, tuple[float, float]]:
    ordered = sorted(bot_ids)
    index_by_bot = {bot_id: index for index, bot_id in enumerate(ordered)}
    size = len(ordered)
    normal = [[0.0 for _ in range(size + 1)] for _ in range(size + 1)]
    right = [0.0 for _ in range(size + 1)]
    fitted_rows: list[tuple[int, int, float, float]] = []

    for observation in observations:
        if observation.bot_a not in bot_ids or observation.bot_b not in bot_ids:
            continue
        left = index_by_bot[observation.bot_a]
        right_index = index_by_bot[observation.bot_b]
        difference = ELO_PER_LOGIT * direction_first_logit(
            observation.margin, margin_scale
        )
        weight = 1.0 / (OUTCOME_STANDARD_ERROR_ELO * OUTCOME_STANDARD_ERROR_ELO)
        normal[left][left] += weight
        normal[right_index][right_index] += weight
        normal[left][right_index] -= weight
        normal[right_index][left] -= weight
        right[left] += weight * difference
        right[right_index] -= weight * difference
        fitted_rows.append((left, right_index, difference, weight))

    # A win against a big field says more than a win against a small one, so
    # the whole component is stretched by how many bots are in it.
    field_scale = field_stretch(size)

    # Fix the otherwise arbitrary rating origin by requiring mean rating 1500.
    for index in range(size):
        normal[index][size] = 1.0
        normal[size][index] = 1.0
    inverse = invert(normal)
    solution = [sum(inverse[row][column] * right[column] for column in range(size + 1))
                for row in range(size + 1)]

    degrees_of_freedom = len(fitted_rows) - (size - 1)
    chi_squared = sum(
        weight * (solution[left] - solution[right_index] - observed) ** 2
        for left, right_index, observed, weight in fitted_rows
    )
    disagreement_inflation = (
        max(1.0, chi_squared / degrees_of_freedom)
        if degrees_of_freedom > 0
        else 1.0
    )
    return {
        bot_id: (
            1500.0 + field_scale * solution[index_by_bot[bot_id]],
            field_scale
            * math.sqrt(max(0.0, inverse[index_by_bot[bot_id]][index_by_bot[bot_id]])
                        * disagreement_inflation),
        )
        for bot_id in ordered
    }


def load_observations(
    connection: sqlite3.Connection, profile_ids: list[int]
) -> list[Observation]:
    profile_filter = ""
    parameters: list[int] = []
    if profile_ids:
        profile_filter = " AND m.rule_profile_id IN (" + ",".join("?" * len(profile_ids)) + ")"
        parameters.extend(profile_ids)
    rows = connection.execute(
        """SELECT m.id, m.rule_profile_id, p.big_blind, m.hand_count,
                  a.bot_id, b.bot_id, a.raw_net_chips,
                  v.standard_error_chips, v.hands_per_sample
           FROM matches m
           JOIN rule_profiles p ON p.id = m.rule_profile_id
           JOIN match_players a ON a.match_id = m.id AND a.bot_slot = 0
           JOIN match_players b ON b.match_id = m.id AND b.bot_slot = 1
           JOIN variance_stats v ON v.match_id = m.id AND v.bot_slot = 0
             AND v.result_type = 'raw'
           WHERE 1 = 1""" + profile_filter + " ORDER BY m.id",
        parameters,
    ).fetchall()
    observations = [
        Observation(
            match_id=row[0],
            profile_id=row[1],
            bot_a=row[4],
            bot_b=row[5],
            margin=row[6] / (row[2] * row[3]),
            margin_standard_error=row[7] / (row[2] * row[8]),
        )
        for row in rows
    ]
    seen: dict[tuple[int, int, int], int] = {}
    for observation in observations:
        key = (
            observation.profile_id,
            min(observation.bot_a, observation.bot_b),
            max(observation.bot_a, observation.bot_b),
        )
        if key in seen:
            raise ValueError(
                "ratings require one match per bot pair and rules profile; "
                f"matches {seen[key]} and {observation.match_id} duplicate a pairing"
            )
        seen[key] = observation.match_id
    return observations


def rebuild(
    database: Path,
    profile_ids: list[int],
    margin_scale: float = DEFAULT_MARGIN_SCALE,
) -> list[dict[str, object]]:
    if not math.isfinite(margin_scale) or margin_scale <= 0.0:
        raise ValueError("margin scale must be positive")
    connection = sqlite3.connect(database)
    connection.execute("PRAGMA foreign_keys = ON")
    initialize_database(connection)
    observations = load_observations(connection, profile_ids)
    if not observations:
        connection.close()
        raise ValueError("no matches found for the requested rules profile")

    selected_profiles = sorted({row.profile_id for row in observations})
    output: list[dict[str, object]] = []
    try:
        connection.execute("BEGIN IMMEDIATE")
        for profile_id in selected_profiles:
            connection.execute(
                "DELETE FROM ratings WHERE rule_profile_id = ?", (profile_id,)
            )
            profile_observations = [
                row for row in observations if row.profile_id == profile_id
            ]
            for component_id, member_ids in enumerate(
                components(profile_observations), start=1
            ):
                fitted = fit_component(member_ids, profile_observations, margin_scale)
                for bot_id, (elo, standard_error) in fitted.items():
                    match_count, hand_count, name, sha256 = connection.execute(
                        """SELECT COUNT(DISTINCT m.id), SUM(m.hand_count),
                                  b.name, b.sha256
                           FROM bots b
                           JOIN match_players mp ON mp.bot_id = b.id
                           JOIN matches m ON m.id = mp.match_id
                           WHERE b.id = ? AND m.rule_profile_id = ?""",
                        (bot_id, profile_id),
                    ).fetchone()
                    lower = elo - 1.96 * standard_error
                    upper = elo + 1.96 * standard_error
                    connection.execute(
                        """INSERT INTO ratings(
                             rule_profile_id, bot_id, rating_version,
                             component_id, margin_scale_bb_per_hand, elo,
                             standard_error, lower_95, upper_95, match_count,
                             hand_count) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
                        (
                            profile_id,
                            bot_id,
                            RATING_VERSION,
                            component_id,
                            margin_scale,
                            elo,
                            standard_error,
                            lower,
                            upper,
                            match_count,
                            hand_count,
                        ),
                    )
                    output.append(
                        {
                            "profile_id": profile_id,
                            "component_id": component_id,
                            "bot_id": bot_id,
                            "name": name,
                            "sha256": sha256,
                            "elo": elo,
                            "standard_error": standard_error,
                            "lower_95": lower,
                            "upper_95": upper,
                            "match_count": match_count,
                            "hand_count": hand_count,
                        }
                    )
        connection.commit()
    except Exception:
        connection.rollback()
        raise
    finally:
        connection.close()
    return sorted(
        output,
        key=lambda row: (row["profile_id"], row["component_id"], -row["elo"]),
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build Elo-scale ratings and uncertainty from Felt matches."
    )
    parser.add_argument("--database", type=Path, default=Path("data/felt.sqlite3"))
    parser.add_argument("--profile", type=int, action="append", default=[])
    parser.add_argument(
        "--margin-scale",
        type=float,
        default=DEFAULT_MARGIN_SCALE,
        help="bb/hand scale for the bounded margin bonus (default: 1)",
    )
    arguments = parser.parse_args()
    try:
        rows = rebuild(arguments.database, arguments.profile, arguments.margin_scale)
        active: tuple[object, object] | None = None
        for row in rows:
            group = (row["profile_id"], row["component_id"])
            if group != active:
                print(f"profile={group[0]} component={group[1]}")
                active = group
            print(
                f"  {row['name']:<20} {row['elo']:8.1f} "
                f"95% [{row['lower_95']:.1f}, {row['upper_95']:.1f}] "
                f"matches={row['match_count']} hands={row['hand_count']} "
                f"sha256={str(row['sha256'])[:12]}"
            )
    except (OSError, ValueError, sqlite3.Error) as error:
        parser.exit(1, f"rebuild_ratings: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
