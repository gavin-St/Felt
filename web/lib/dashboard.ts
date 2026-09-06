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
const BASE_WIN_LOGIT = 1;
const MARGIN_BONUS_LOGIT = 0.15;
const OUTCOME_STANDARD_ERROR_ELO = 100;
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
 * The two poles are a validated diverging pair -- the green is unchanged, the
 * red moved from #d14f48 to #c1441f, which lifts deuteranope separation from
 * dE 5.1 (a fail) to 9.6 (a pass) at the same warmth. The surface itself is
 * the neutral midpoint, and every cell prints its signed number, so polarity
 * never rests on hue alone.
 *
 * Ink stays dark at every step. Against both poles at full strength, near
 * black holds about 5:1 while white manages 3.2 on the green and drops under
 * 3 on the red, so flipping to light type made the strongest cells the
 * hardest to read.
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
  const hue = value > 0 ? '#18a56b' : '#c1441f';
  return {
    background: `color-mix(in oklab, ${hue} ${alpha.toFixed(1)}%, transparent)`,
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
  rawBb: number;
  preflopBb: number;
  showdownBb: number;
  postflopNonshowdownBb: number;
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

/* Chips actually contested, not hands.final_pot_chips: that figure counts an
 * uncalled bet, so a 200 bb shove folded to would read as a 201 bb pot. The
 * exporter derives it; a snapshot taken before that is missing the field, so
 * read it defensively and let the stat show a dash rather than a wrong number. */
function potChips(player: MatchPlayer): number | null {
  const value = (player as Partial<Record<'contested_pot_chips_total', number>>)
    .contested_pot_chips_total;
  return typeof value === 'number' ? value : null;
}

function share(part: number, whole: number) {
  return whole === 0 ? null : (100 * part) / whole;
}

export function statBlock(entries: PlayerEntry[]): StatBlock {
  let hands = 0;
  let rawBb = 0;
  let preflopBb = 0;
  let showdownBb = 0;
  let nonshowdownBb = 0;
  let preflopHands = 0;
  let sawFlop = 0;
  let showdowns = 0;
  let showdownWins = 0;
  let vpip = 0;
  let pfr = 0;
  let cbets = 0;
  let cbetOpportunities = 0;
  let allInReached = 0;
  let aggressive = 0;
  let decisions = 0;
  let potBb = 0;
  let potHands = 0;

  for (const { player, bigBlind } of entries) {
    hands += player.hands;
    rawBb += player.raw_net_chips / bigBlind;
    preflopBb += player.preflop_raw_net_chips / bigBlind;
    showdownBb += player.showdown_raw_net_chips / bigBlind;
    nonshowdownBb += player.nonshowdown_raw_net_chips / bigBlind;
    preflopHands += player.preflop_hands;
    sawFlop += player.saw_flop;
    showdowns += player.showdowns;
    showdownWins += player.showdown_wins;
    vpip += player.vpip;
    pfr += player.pfr;
    cbets += player.cbets;
    cbetOpportunities += player.cbet_opportunities;
    allInReached += player.all_in_reached;
    const pot = potChips(player);
    if (pot !== null) {
      potBb += pot / bigBlind;
      potHands += player.hands;
    }
    /* Action types: fold 1, check 2, call 3, raise 4. Aggression frequency is
     * bets and raises over every decision that was not a check. */
    for (const action of player.actions) {
      if (action.action_type === 4) {
        aggressive += action.count;
        decisions += action.count;
      } else if (action.action_type === 1 || action.action_type === 3) {
        decisions += action.count;
      }
    }
  }

  return {
    matches: entries.length,
    hands,
    rawBb,
    preflopBb,
    showdownBb,
    postflopNonshowdownBb: nonshowdownBb - preflopBb,
    preflopHands,
    bbPerHand: hands === 0 ? null : rawBb / hands,
    preflopShare: share(preflopHands, hands),
    showdownShare: share(showdowns, hands),
    postflopNonshowdownShare: share(hands - preflopHands - showdowns, hands),
    vpip: share(vpip, hands),
    pfr: share(pfr, hands),
    aggression: share(aggressive, decisions),
    cbet: share(cbets, cbetOpportunities),
    wtsd: share(showdowns, sawFlop),
    allInReached: share(allInReached, hands),
    wsd: share(showdownWins, showdowns),
    averagePotBb: potHands === 0 ? null : potBb / potHands,
  };
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
