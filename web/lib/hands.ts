/*
 * The hand ledger, unlike everything else on this site, is not in the bundle.
 *
 * `dashboard.ts` imports its snapshot at build time, which is why the matchup
 * matrix needs no server at all. That cannot scale to 4.62 million hands, and
 * the deployed site is a Worker with no filesystem, so it could not read the
 * ledger even if the file were shipped. The replay browser therefore talks to
 * `scripts/hand_server.py`, which runs on the machine that has the database.
 *
 * So the feature is local by construction. In `npm run dev` it is on; in a
 * production build `process.env.NODE_ENV` is 'production', every entry point
 * disappears, and the published site has no replay rather than a broken one.
 * Setting `globalThis.FELT_HAND_API` in the console overrides the address for
 * the rare case of pointing a local page at a server on another port.
 */

export const HAND_REPLAY_ENABLED = process.env.NODE_ENV !== 'production';

const DEFAULT_HAND_API = 'http://127.0.0.1:8899';

export function handApi() {
  const override = (globalThis as { FELT_HAND_API?: string }).FELT_HAND_API;
  return override ?? DEFAULT_HAND_API;
}

export type HandFilter =
  | 'showdown'
  | 'no-showdown'
  | 'preflop'
  | 'all-in'
  | 'three-bet'
  | 'big-pot'
  | 'won'
  | 'lost'
  | 'cbet'
  | 'opponent-folded'
  | 'hero-folded';

/* Label, and what the filter actually tests, since several of these are easy
 * to read as something slightly different from what the SQL asks. */
export const HAND_FILTERS: Array<[HandFilter, string, string]> = [
  ['showdown', 'Showdown', 'Both hands were shown'],
  ['no-showdown', 'No showdown', 'Saw a flop, someone folded'],
  ['preflop', 'Ended preflop', 'Never saw a flop'],
  ['all-in', 'All-in', 'Stacks went in'],
  ['three-bet', '3-bet+', 'Three-bet or bigger preflop'],
  ['big-pot', 'Big pot', 'Final pot 40 BB or more'],
  ['won', 'Hero won', 'Positive result for the hero bot'],
  ['lost', 'Hero lost', 'Negative result for the hero bot'],
  ['cbet', 'Hero c-bet', 'Hero bet the flop as preflop raiser'],
  ['opponent-folded', 'Opponent folded', 'The other bot gave it up'],
  ['hero-folded', 'Hero folded', 'The hero bot gave it up'],
];

export const HAND_SORTS: Array<[string, string]> = [
  ['random', 'Shuffled'],
  ['pot', 'Biggest pot'],
  ['won-most', 'Hero won most'],
  ['lost-most', 'Hero lost most'],
  ['hand-order', 'Deal order'],
];

export type HandSummary = {
  match_id: number;
  hand_index: number;
  bot_id: number;
  opponent_bot_id: number;
  position: number;
  bucket: string;
  exact_combo: string;
  outcome: 'win' | 'loss' | 'chop';
  raw_net_chips: number;
  adjusted_net_chips: number;
  showdown: number;
  showdown_win: number;
  all_in_reached: number;
  cbet_made: number;
  pot_class: string;
  final_pot_chips: number;
  saw_flop: number;
  ending_street: number;
  end_reason: number;
  folded_position: number | null;
  board: string[];
  bot_name: string;
  opponent_name: string;
};

export type HandEvent = {
  position: number;
  street: number;
  type: number;
  amount_to: number;
};

export type HandDecision = {
  position: number;
  street: number;
  legal_actions: number;
  pot: number;
  my_stack: number;
  opp_stack: number;
  my_street_contribution: number;
  opp_street_contribution: number;
  to_call: number;
  min_raise_to: number;
  max_raise_to: number;
  decision_random: number;
  requested: { type: number; amount_to: number };
  applied: { type: number; amount_to: number };
  violation: number;
  cpu_time_ns: number;
  wall_time_ns: number;
};

export type HandPlayer = {
  bot_slot: number;
  bot_id: number;
  name: string;
  position: number;
  bucket: string;
  exact_combo: string;
  outcome: 'win' | 'loss' | 'chop';
  raw_net_chips: number;
  adjusted_net_chips: number;
  showdown_win: number;
  hole: string[];
};

export type HandDetail = {
  match_id: number;
  hand_index: number;
  board: string[];
  bot_by_position: number[];
  events: HandEvent[];
  decisions: HandDecision[];
  players: HandPlayer[];
  summary: {
    end_reason: number;
    ending_street: number;
    folded_position: number | null;
    pot_class: string;
    showdown: number;
    final_pot_chips: number;
    preflop_raise_count: number;
    all_in_street: number | null;
    hand_count: number;
    big_blind: number;
    small_blind: number;
    starting_stack: number;
  };
};

export type HandMeta = {
  big_blind: number;
  small_blind: number;
  starting_stack: number;
  bots: Array<{ id: number; name: string; matches: number; hands: number }>;
  matchups: Array<{
    match_id: number;
    hand_count: number;
    bot_id: number;
    bot_name: string;
    opponent_bot_id: number;
    opponent_name: string;
  }>;
};

async function get<T>(path: string): Promise<T> {
  const response = await fetch(`${handApi()}${path}`, { cache: 'no-store' });
  const payload = (await response.json()) as { error?: string };
  if (!response.ok) throw new Error(payload.error ?? response.statusText);
  return payload as T;
}

export const fetchHandMeta = () => get<HandMeta>('/api/meta');

export function fetchHands(query: {
  bot?: number;
  opponent?: number;
  match?: number;
  hand?: string;
  filters?: HandFilter[];
  sort?: string;
  limit?: number;
  offset?: number;
}) {
  const search = new URLSearchParams();
  if (query.bot) search.set('bot', String(query.bot));
  if (query.opponent) search.set('opponent', String(query.opponent));
  if (query.match) search.set('match', String(query.match));
  if (query.hand) search.set('hand', query.hand);
  if (query.filters?.length) search.set('filter', query.filters.join(','));
  search.set('sort', query.sort ?? 'random');
  search.set('limit', String(query.limit ?? 20));
  search.set('offset', String(query.offset ?? 0));
  return get<{
    total: number;
    limit: number;
    offset: number;
    hands: HandSummary[];
  }>(`/api/hands?${search}`);
}

export const fetchHand = (matchId: number, handIndex: number) =>
  get<HandDetail>(`/api/hand/${matchId}/${handIndex}`);

/* --- shared vocabulary ------------------------------------------------- */

export const STREETS = ['Preflop', 'Flop', 'Turn', 'River'];

export const EVENT_NAMES: Record<number, string> = {
  1: 'posts small blind',
  2: 'posts big blind',
  3: 'folds',
  4: 'checks',
  5: 'calls',
  6: 'bets',
  7: 'raises to',
};

export const ACTION_NAMES: Record<number, string> = {
  1: 'FOLD',
  2: 'CHECK',
  3: 'CALL',
  4: 'RAISE_TO',
};

export const isBlind = (event: HandEvent) => event.type === 1 || event.type === 2;
export const isAggressive = (event: HandEvent) => event.type === 6 || event.type === 7;

export function legalActionNames(mask: number) {
  const names: string[] = [];
  if (mask & 1) names.push('fold');
  if (mask & 2) names.push('check');
  if (mask & 4) names.push('call');
  if (mask & 8) names.push('raise');
  return names;
}

/* The same arithmetic felt_call_price_percent does: the share of the pot you
 * would be playing for that the call itself costs. state.pot already contains
 * their bet, so a three-quarter-pot bet is 30 percent, not 42. */
export function callPricePercent(decision: HandDecision) {
  if (decision.to_call <= 0) return 0;
  return (100 * decision.to_call) / (decision.pot + decision.to_call);
}

export const bb = (chips: number, bigBlind: number) => chips / bigBlind;

export function suitOf(card: string) {
  return card.slice(-1) as 'c' | 'd' | 's' | 'h';
}

export const SUIT_GLYPHS: Record<string, string> = {
  c: '♣',
  d: '♦',
  s: '♠',
  h: '♥',
};

export const suitIsRed = (card: string) =>
  suitOf(card) === 'd' || suitOf(card) === 'h';

/*
 * One frame per event, plus a frame zero for the deal, so stepping is just an
 * index. Every quantity a view needs is derived here rather than in the views:
 * the two of them disagreeing about the pot would be the one bug worth being
 * careful about, since neither would look wrong on its own.
 */
export type Frame = {
  step: number;
  street: number;
  boardCount: number;
  event: HandEvent | null;
  decision: HandDecision | null;
  streetContribution: [number, number];
  committed: [number, number];
  pot: number;
  stacks: [number, number];
  actor: number | null;
};

export function buildFrames(hand: HandDetail): Frame[] {
  const stack = hand.summary.starting_stack;
  const frames: Frame[] = [
    {
      step: 0,
      street: 0,
      boardCount: 0,
      event: null,
      decision: null,
      streetContribution: [0, 0],
      committed: [0, 0],
      pot: 0,
      stacks: [stack, stack],
      actor: null,
    },
  ];

  const streetContribution: [number, number] = [0, 0];
  const settled: [number, number] = [0, 0];
  let street = 0;
  let decisionIndex = 0;

  for (const event of hand.events) {
    if (event.street !== street) {
      settled[0] += streetContribution[0];
      settled[1] += streetContribution[1];
      streetContribution[0] = 0;
      streetContribution[1] = 0;
      street = event.street;
    }
    streetContribution[event.position] = Math.max(
      streetContribution[event.position],
      event.amount_to,
    );
    const decision = isBlind(event) ? null : hand.decisions[decisionIndex++] ?? null;
    const committed: [number, number] = [
      settled[0] + streetContribution[0],
      settled[1] + streetContribution[1],
    ];
    frames.push({
      step: frames.length,
      street,
      boardCount: street === 0 ? 0 : street + 2,
      event,
      decision,
      streetContribution: [...streetContribution] as [number, number],
      committed,
      pot: committed[0] + committed[1],
      stacks: [stack - committed[0], stack - committed[1]],
      actor: event.position,
    });
  }
  return frames;
}

export function describeEvent(event: HandEvent, bigBlind: number) {
  const name = EVENT_NAMES[event.type] ?? 'acts';
  if (event.type === 3 || event.type === 4) return name;
  return `${name} ${(event.amount_to / bigBlind).toFixed(1)} BB`;
}
