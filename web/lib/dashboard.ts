import snapshot from '@/data/dashboard.json';

export type Rating = (typeof snapshot.ratings)[number];
export type MatrixResult = (typeof snapshot.matrix)[number];
export type MatchDetail = (typeof snapshot.matches)[number];

export const dashboard = snapshot;

export function signed(value: number, digits = 2) {
  return `${value > 0 ? '+' : ''}${value.toFixed(digits)}`;
}

export function matrixResult(botId: number, opponentId: number) {
  return dashboard.matrix.find(
    (result) =>
      result.bot_id === botId && result.opponent_bot_id === opponentId,
  );
}

const ELO_PER_LOGIT = 400 / Math.log(10);
const BASE_WIN_LOGIT = 4;
const MARGIN_BONUS_LOGIT = 0.6;
const OUTCOME_STANDARD_ERROR_ELO = 400;
const MARGIN_ONLY_MAX_ELO = 400;
const MARGIN_ONLY_SCALE_BB = 10;

export type RatingFormula = 'outcome-first' | 'margin-only';

function solveLinear(matrix: number[][], values: number[]) {
  const size = matrix.length;
  const augmented = matrix.map((row, index) => [...row, values[index]]);

  for (let column = 0; column < size; column += 1) {
    let pivot = column;
    for (let row = column + 1; row < size; row += 1) {
      if (
        Math.abs(augmented[row][column]) > Math.abs(augmented[pivot][column])
      ) {
        pivot = row;
      }
    }
    if (Math.abs(augmented[pivot][column]) < 1e-12) {
      throw new Error('Rating graph produced a singular system');
    }
    [augmented[column], augmented[pivot]] = [
      augmented[pivot],
      augmented[column],
    ];
    const divisor = augmented[column][column];
    augmented[column] = augmented[column].map((value) => value / divisor);

    for (let row = 0; row < size; row += 1) {
      if (row === column) continue;
      const multiplier = augmented[row][column];
      if (multiplier === 0) continue;
      augmented[row] = augmented[row].map(
        (value, index) => value - multiplier * augmented[column][index],
      );
    }
  }

  return augmented.map((row) => row[size]);
}

export function subsetRatings(
  botIds: Set<number>,
  formula: RatingFormula = 'outcome-first',
) {
  const observations = new Map<number, MatrixResult>();
  for (const result of dashboard.matrix) {
    if (
      botIds.has(result.bot_id) &&
      botIds.has(result.opponent_bot_id) &&
      !observations.has(result.match_id)
    ) {
      observations.set(result.match_id, result);
    }
  }

  const neighbors = new Map<number, Set<number>>();
  for (const botId of botIds) neighbors.set(botId, new Set());
  for (const observation of observations.values()) {
    neighbors.get(observation.bot_id)?.add(observation.opponent_bot_id);
    neighbors.get(observation.opponent_bot_id)?.add(observation.bot_id);
  }

  const ratings = new Map<number, number>();
  const unseen = new Set(botIds);
  while (unseen.size > 0) {
    const start = Math.min(...unseen);
    const pending = [start];
    const component = new Set<number>();
    while (pending.length > 0) {
      const botId = pending.pop()!;
      if (component.has(botId)) continue;
      component.add(botId);
      unseen.delete(botId);
      for (const neighbor of neighbors.get(botId) ?? []) {
        if (!component.has(neighbor)) pending.push(neighbor);
      }
    }

    const ordered = [...component].sort((left, right) => left - right);
    const indexByBot = new Map(ordered.map((botId, index) => [botId, index]));
    const size = ordered.length;
    const normal = Array.from({ length: size + 1 }, () =>
      Array.from({ length: size + 1 }, () => 0),
    );
    const right = Array.from({ length: size + 1 }, () => 0);

    for (const observation of observations.values()) {
      const left = indexByBot.get(observation.bot_id);
      const rightIndex = indexByBot.get(observation.opponent_bot_id);
      if (left === undefined || rightIndex === undefined) continue;
      const difference =
        formula === 'margin-only'
          ? MARGIN_ONLY_MAX_ELO *
            Math.tanh(observation.adjusted_bb_per_hand / MARGIN_ONLY_SCALE_BB)
          : ELO_PER_LOGIT *
            (observation.raw_bb_per_hand === 0
              ? 0
              : Math.sign(observation.raw_bb_per_hand) *
                (BASE_WIN_LOGIT +
                  MARGIN_BONUS_LOGIT *
                    Math.tanh(Math.abs(observation.raw_bb_per_hand))));
      const weight =
        1 / (OUTCOME_STANDARD_ERROR_ELO * OUTCOME_STANDARD_ERROR_ELO);
      normal[left][left] += weight;
      normal[rightIndex][rightIndex] += weight;
      normal[left][rightIndex] -= weight;
      normal[rightIndex][left] -= weight;
      right[left] += weight * difference;
      right[rightIndex] -= weight * difference;
    }

    for (let index = 0; index < size; index += 1) {
      normal[index][size] = 1;
      normal[size][index] = 1;
    }
    const solution = solveLinear(normal, right);
    ordered.forEach((botId, index) =>
      ratings.set(botId, 1500 + solution[index]),
    );
  }

  return [...dashboard.ratings]
    .filter((bot) => botIds.has(bot.bot_id))
    .map((bot) => ({ ...bot, elo: ratings.get(bot.bot_id) ?? 1500 }))
    .sort(
      (left, right) =>
        right.elo - left.elo || left.name.localeCompare(right.name),
    );
}

/*
 * Matrix cell colour. Magnitude is mapped logarithmically, not linearly: a
 * linear ramp wastes almost the whole scale on margins that only crude bots
 * produce, so +0.01 and +0.5 come out the same faint green even though one is
 * noise and the other is a real edge. ln(1 + |v|/0.2) spends most of the ramp
 * below 2 bb/hand and still leaves headroom past 20, which is where this goes
 * as the bots stop being terrible.
 *
 * The surface itself is the neutral midpoint, and every cell prints its signed
 * number, so polarity never rests on hue alone -- which matters, because this
 * green and red separate by only dE 5.1 for a deuteranope.
 */
const TONE_SCALE_BB = 0.2;
const TONE_CEILING_BB = 20;
const TONE_MIN_ALPHA = 4;
const TONE_MAX_ALPHA = 82;

export function toneStrength(value: number) {
  const magnitude = Math.min(Math.abs(value), TONE_CEILING_BB);
  return (
    Math.log1p(magnitude / TONE_SCALE_BB) /
    Math.log1p(TONE_CEILING_BB / TONE_SCALE_BB)
  );
}

export function resultTone(value: number) {
  if (value === 0) {
    return { background: 'color-mix(in oklab, currentColor 5%, transparent)' };
  }
  const alpha =
    TONE_MIN_ALPHA + toneStrength(value) * (TONE_MAX_ALPHA - TONE_MIN_ALPHA);
  /* Light ink from the same alpha the linear ramp used to flip at, so the
   * switch lands on the same cells it always did. */
  const inverted = alpha >= 52;
  if (value > 0) {
    return {
      background: `color-mix(in oklab, #18a56b ${alpha.toFixed(1)}%, transparent)`,
      color: inverted ? '#f4fff8' : 'inherit',
    };
  }
  return {
    background: `color-mix(in oklab, #d14f48 ${alpha.toFixed(1)}%, transparent)`,
    color: inverted ? '#fff7f6' : 'inherit',
  };
}

/* ------------------------------------------------------------------ */
/* Shared stat block                                                    */
/*                                                                      */
/* The matchup page reads one match, a bot page reads every match that  */
/* bot played. Both go through here so a number never means two things. */
/* Percentage denominators follow v_match_bot_stats: VPIP and PFR are   */
/* shares of hands dealt, WTSD is a share of flops seen, W$SD a share   */
/* of showdowns, and c-bet a share of c-bet opportunities.              */
/* ------------------------------------------------------------------ */

export type MatchPlayer = MatchDetail['players'][number];
export type PlayerEntry = { player: MatchPlayer; bigBlind: number };

export type StatBlock = {
  matches: number;
  hands: number;
  rawBb: number | null;
  preflopBb: number | null;
  showdownBb: number | null;
  postflopNonshowdownBb: number | null;
  preflopBbPerHand: number | null;
  showdownBbPerHand: number | null;
  postflopNonshowdownBbPerHand: number | null;
  preflopHands: number;
  bbPerHand: number | null;
  preflopShare: number | null;
  showdownShare: number | null;
  postflopNonshowdownShare: number | null;
  vpip: number | null;
  pfr: number | null;
  aggression: number | null;
  cbet: number | null;
  wtsd: number | null;
  allInReached: number | null;
  wsd: number | null;
  averagePotBb: number | null;
};

/*
 * Every number below is read, never computed. Their definitions live in
 * v_match_bot_stats and v_bot_totals, which are built from the stored match
 * facts by rebuild_statistics and checked by validate_statistics; the exporter
 * copies them into the snapshot verbatim. Deriving any of them a second time
 * here would mean a published figure with two definitions and nothing keeping
 * them in step.
 *
 * A snapshot written before those columns existed simply lacks the field, and
 * a missing field reads as a dash rather than as a wrong number. Re-run
 * scripts/rebuild_stats.py and web/scripts/export_dashboard.py to fill them.
 */
type StatRow = Record<string, unknown>;

function num(row: StatRow, key: string): number | null {
  const value = row[key];
  return typeof value === 'number' && Number.isFinite(value) ? value : null;
}

function statBlockFromRow(row: StatRow, matches: number): StatBlock {
  return {
    matches,
    hands: num(row, 'hands') ?? 0,
    rawBb: num(row, 'raw_bb'),
    preflopBb: num(row, 'preflop_bb'),
    showdownBb: num(row, 'showdown_bb'),
    postflopNonshowdownBb: num(row, 'postflop_nonshowdown_bb'),
    preflopBbPerHand: num(row, 'preflop_bb_per_hand'),
    showdownBbPerHand: num(row, 'showdown_bb_per_hand'),
    postflopNonshowdownBbPerHand: num(row, 'postflop_nonshowdown_bb_per_hand'),
    preflopHands: num(row, 'preflop_hands') ?? 0,
    bbPerHand: num(row, 'raw_bb_per_hand'),
    preflopShare: num(row, 'preflop_percentage'),
    showdownShare: num(row, 'showdown_percentage'),
    postflopNonshowdownShare: num(row, 'postflop_nonshowdown_percentage'),
    vpip: num(row, 'vpip_percentage'),
    pfr: num(row, 'pfr_percentage'),
    aggression: num(row, 'aggression_percentage'),
    cbet: num(row, 'cbet_percentage'),
    wtsd: num(row, 'wtsd_percentage'),
    allInReached: num(row, 'all_in_reached_percentage'),
    wsd: num(row, 'w_sd_percentage'),
    averagePotBb: num(row, 'average_pot_bb'),
  };
}

/* One matchup, from that match's own row. */
export function matchStats(player: MatchPlayer): StatBlock {
  return statBlockFromRow(player as StatRow, 1);
}

/* One bot across every match it has played, from the roll-up view. */
export function botStats(botId: number): StatBlock {
  const totals = (
    (snapshot as { bot_totals?: StatRow[] }).bot_totals ?? []
  ).find((row) => row.bot_id === botId);
  if (!totals) {
    return statBlockFromRow({}, 0);
  }
  return statBlockFromRow(totals, num(totals, 'match_count') ?? 0);
}

export type AggregateBucket = {
  bucket: string;
  hands: number;
  adjustedBb: number;
  adjustedBbPerHand: number;
};

export function aggregateBuckets(entries: PlayerEntry[]): AggregateBucket[] {
  const totals = new Map<string, { hands: number; adjustedBb: number }>();
  for (const { player, bigBlind } of entries) {
    for (const bucket of player.buckets) {
      const running = totals.get(bucket.bucket) ?? { hands: 0, adjustedBb: 0 };
      running.hands += bucket.hands;
      running.adjustedBb += bucket.adjusted_net_chips / bigBlind;
      totals.set(bucket.bucket, running);
    }
  }
  return [...totals.entries()]
    .map(([bucket, running]) => ({
      bucket,
      hands: running.hands,
      adjustedBb: running.adjustedBb,
      adjustedBbPerHand:
        running.hands === 0 ? 0 : running.adjustedBb / running.hands,
    }))
    .filter((bucket) => bucket.hands > 0)
    .sort((left, right) => right.adjustedBb - left.adjustedBb);
}

/* Every match the bot played, newest ledger entry last. */
export function botEntries(botId: number): PlayerEntry[] {
  const entries: PlayerEntry[] = [];
  for (const match of dashboard.matches) {
    for (const player of match.players) {
      if (player.bot_id === botId) {
        entries.push({ player, bigBlind: match.big_blind });
      }
    }
  }
  return entries;
}
