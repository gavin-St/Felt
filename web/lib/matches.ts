import { dashboard } from '@/lib/dashboard';
import type { PlayerEntry } from '@/lib/dashboard';

/*
 * Per-match detail: every player's buckets, actions, timings and pot classes.
 *
 * One file per match under public/data/matches, rather than one file holding
 * all of them, and fetched rather than bundled. The combined file was thirty
 * megabytes and ninety-eight per cent starting-hand buckets -- 169 of them per
 * player per match -- so bundling it would have shipped every match to every
 * visitor to render one, and rewriting it on each rerun made git store the
 * whole snapshot again to record eighty kilobytes of change. Split and
 * fetched, a matchup page pulls the single 78 kB file it needs and a rerun
 * touches only the matches that were rerun.
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
 * Fetched on demand and remembered for the session. Two pages ask for the same
 * match often -- both sides of a pairing, and a bot page walking its opponents
 * -- and a match that has been read once is a match whose file cannot change
 * until the site is rebuilt, so there is nothing to invalidate.
 */
const cache = new Map<number, Promise<MatchDetail | undefined>>();

function matchUrl(id: number) {
  /* BASE_URL carries the trailing slash, and is '/' when the site is served
   * from a domain root. */
  return `${import.meta.env.BASE_URL}data/matches/${id}.json`;
}

export async function matchById(id: number): Promise<MatchDetail | undefined> {
  if (!Number.isInteger(id)) return undefined;
  const known = cache.get(id);
  if (known) return known;
  const pending = fetch(matchUrl(id))
    .then((response) =>
      response.ok ? (response.json() as Promise<MatchDetail>) : undefined,
    )
    .catch(() => undefined);
  cache.set(id, pending);
  return pending;
}

/*
 * Every match the bot played. The match ids come from the matrix in
 * dashboard.json, which is already in the bundle and carries one row per
 * pairing, so a bot's twenty-eight matchups cost twenty-eight small requests
 * rather than a scan of all four hundred files.
 */
export async function botEntries(botId: number): Promise<PlayerEntry[]> {
  const ids = new Set(
    dashboard.matrix
      .filter((result) => result.bot_id === botId)
      .map((result) => result.match_id),
  );
  const loaded = await Promise.all([...ids].map(matchById));
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
