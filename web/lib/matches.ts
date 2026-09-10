import { dashboard } from '@/lib/dashboard';
import type { PlayerEntry } from '@/lib/dashboard';

/*
 * Per-match detail: every player's buckets, actions, timings and pot classes.
 *
 * One file per match, under data/matches, rather than one file holding all of
 * them. The combined file was thirty megabytes and ninety-eight per cent
 * starting-hand buckets -- 169 of them per player per match -- which made
 * every rerun of a single matchup rewrite the whole thing, so git stored
 * another thirty megabytes to record eighty kilobytes of change. Split, a
 * rerun touches exactly the matches that were rerun, and a matchup page
 * fetches one match instead of all four hundred.
 *
 * Only server components may import this. The pages that need it all render
 * on the server, and keeping it out of @/lib/dashboard is what keeps it out
 * of the browser bundle, where the combined file was a 27.7 MiB chunk that
 * Cloudflare refused to serve at all.
 */

export type MatchBucket = {
  bucket: string;
  hands: number;
  raw_net_chips: number;
  adjusted_net_chips: number;
  wins: number;
  losses: number;
  chops: number;
  all_in_reached: number;
  raw_bb_per_hand: number;
  adjusted_bb_per_hand: number;
};

export type MatchActionCount = {
  street: number;
  action_type: number;
  count: number;
  street_decisions: number;
};

export type MatchPotClass = {
  pot_class: string;
  count: number;
  opportunities: number;
};

export type MatchPositionSplit = {
  position: number;
  hands: number;
  raw_net_chips: number;
  adjusted_net_chips: number;
};

export type MatchViolation = {
  violation: number;
  count: number;
  decisions: number;
};

export type MatchTiming = {
  match_id: number;
  bot_slot: number;
  decisions: number;
  mean_wall_time_ns: number;
  p99_wall_time_ns: number;
  max_wall_time_ns: number;
  mean_cpu_time_ns: number;
  p99_cpu_time_ns: number;
  max_cpu_time_ns: number;
  violations: number;
};

/*
 * The four nullable numbers are rates with no denominator rather than rates
 * of zero: a bot that never had a c-bet opportunity has no c-bet percentage,
 * and printing 0% would say it declined chances it never got.
 */
export type MatchPlayer = {
  match_id: number;
  bot_slot: number;
  bot_id: number;
  bot_name: string;
  bot_sha256: string;
  big_blind: number;
  stats_version: number;
  hands: number;
  wins: number;
  losses: number;
  chops: number;
  raw_net_chips: number;
  adjusted_net_chips: number;
  raw_bb: number;
  raw_bb_per_hand: number;
  adjusted_bb_per_hand: number;
  raw_win_percentage: number;
  vpip: number;
  vpip_percentage: number;
  pfr: number;
  pfr_percentage: number;
  saw_flop: number;
  showdowns: number;
  showdown_wins: number;
  showdown_percentage: number;
  wtsd_percentage: number | null;
  w_sd_percentage: number | null;
  cbets: number;
  cbet_opportunities: number;
  cbet_percentage: number | null;
  aggressive_actions: number;
  aggression_decisions: number;
  aggression_percentage: number;
  all_ins: MatchActionCount[];
  all_in_initiated: number;
  all_in_initiated_percentage: number;
  all_in_reached: number;
  all_in_reached_percentage: number;
  average_pot_bb: number;
  average_exact_equity: number | null;
  exact_equity_hands: number;
  contested_pot_chips: number;
  preflop_hands: number;
  preflop_raw_net_chips: number;
  preflop_adjusted_net_chips: number;
  preflop_bb: number;
  preflop_bb_per_hand: number;
  preflop_percentage: number;
  showdown_raw_net_chips: number;
  showdown_adjusted_net_chips: number;
  showdown_bb: number;
  showdown_bb_per_hand: number;
  nonshowdown_raw_net_chips: number;
  nonshowdown_adjusted_net_chips: number;
  postflop_nonshowdown_raw_net_chips: number;
  postflop_nonshowdown_bb: number;
  postflop_nonshowdown_bb_per_hand: number;
  postflop_nonshowdown_percentage: number;
  actions: MatchActionCount[];
  positions: MatchPositionSplit[];
  pot_classes: MatchPotClass[];
  timing: MatchTiming | null;
  violations_by_code: MatchViolation[];
  buckets: MatchBucket[];
};

export type MatchDetail = {
  id: number;
  hand_count: number;
  small_blind: number;
  big_blind: number;
  starting_stack: number;
  duplicate: number;
  equity_adjustment: number;
  decision_cap_us: number;
  match_seed: string;
  harness_version: string;
  imported_at: string;
  players: MatchPlayer[];
};

/*
 * A lazy loader per file, so a page pays only for the matches it names. Vite
 * turns each of these into its own chunk at build time, which is also what
 * makes this work on the Worker, where there is no filesystem to read from.
 */
const files = import.meta.glob<{ default: MatchDetail }>(
  '../data/matches/*.json',
);

const loaders = new Map<number, () => Promise<{ default: MatchDetail }>>();
for (const [path, load] of Object.entries(files)) {
  const id = Number(/(\d+)\.json$/.exec(path)?.[1]);
  if (Number.isInteger(id)) loaders.set(id, load);
}

/** Every match id the export wrote, ascending. */
export const matchIds = [...loaders.keys()].sort((left, right) => left - right);

export async function matchById(id: number): Promise<MatchDetail | undefined> {
  const load = loaders.get(id);
  if (!load) return undefined;
  return (await load()).default;
}

/*
 * Every match the bot played, in ranking order of opponent. The match ids
 * come from the matrix in dashboard.json rather than from opening all four
 * hundred files and looking: the matrix already carries one row per pairing,
 * so a bot's twenty-eight matchups cost twenty-eight small reads.
 */
export async function botEntries(botId: number): Promise<PlayerEntry[]> {
  const ids = dashboard.matrix
    .filter((result) => result.bot_id === botId)
    .map((result) => result.match_id);
  const loaded = await Promise.all([...new Set(ids)].map(matchById));
  const entries: PlayerEntry[] = [];
  for (const match of loaded) {
    if (!match) continue;
    for (const player of match.players) {
      if (player.bot_id === botId) {
        entries.push({ player, bigBlind: match.big_blind });
      }
    }
  }
  return entries;
}
