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
  | 'postflop'
  | 'postflop-no-showdown'
  | 'preflop'
  | 'all-in'
  | 'three-bet'
  | 'four-bet'
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
  ['postflop', 'Saw a flop', 'Reached the flop, shown down or not'],
  ['postflop-no-showdown', 'Flop, no showdown', 'Saw a flop, then someone folded'],
  ['preflop', 'Ended preflop', 'Never saw a flop'],
  ['all-in', 'All-in', 'Stacks went in'],
  ['three-bet', '3-bet+', 'Three-bet or bigger preflop'],
  ['four-bet', '4-bet+', 'Four-bet or bigger preflop'],
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
  /* Only set when the hand went all in: the exact share of the pot this hand
   * was worth once no more decisions were possible. */
  exact_equity: number | null;
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
 * One frame per decision. The blinds are not decisions -- nobody was asked --
 * so they are not steps: frame zero is the table with the blinds already in
 * and nothing to say about it yet.
 *
 * Every quantity a view needs is derived here rather than in the view, so
 * there is one place that can be wrong about the pot.
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
  /* Bets and raises already made on this street before this action. */
  priorAggression: number;
  /* A card dealt after the betting was over, because the stacks were in. */
  runout: boolean;
};

export function buildFrames(hand: HandDetail): Frame[] {
  const stack = hand.summary.starting_stack;

  /* Read the posted blinds off the log rather than the rules profile, so a
   * hand played under different blinds still opens correctly. */
  const posted: [number, number] = [0, 0];
  for (const event of hand.events) {
    if (isBlind(event)) posted[event.position] = event.amount_to;
  }

  const frames: Frame[] = [
    {
      step: 0,
      street: 0,
      boardCount: 0,
      event: null,
      decision: null,
      streetContribution: [...posted] as [number, number],
      committed: [...posted] as [number, number],
      pot: posted[0] + posted[1],
      stacks: [stack - posted[0], stack - posted[1]],
      actor: null,
      priorAggression: 0,
      runout: false,
    },
  ];

  const streetContribution: [number, number] = [...posted] as [number, number];
  const settled: [number, number] = [0, 0];
  let street = 0;
  let decisionIndex = 0;
  let aggression = 0;

  for (const event of hand.events) {
    if (isBlind(event)) continue;
    if (event.street !== street) {
      aggression = 0;
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
    const committed: [number, number] = [
      settled[0] + streetContribution[0],
      settled[1] + streetContribution[1],
    ];
    frames.push({
      step: frames.length,
      street,
      boardCount: street === 0 ? 0 : street + 2,
      event,
      decision: hand.decisions[decisionIndex++] ?? null,
      streetContribution: [...streetContribution] as [number, number],
      committed,
      pot: committed[0] + committed[1],
      stacks: [stack - committed[0], stack - committed[1]],
      actor: event.position,
      priorAggression: aggression,
      runout: false,
    });
    if (isAggressive(event)) aggression += 1;
  }

  /*
   * When the stacks go in there are no more decisions, but the cards are
   * still dealt and the hand is still decided by them -- a preflop all-in
   * reaches a showdown with a full board and nothing in the replay was
   * showing it. One frame per remaining card, so the runout can be stepped
   * through like the rest of the hand.
   */
  if (hand.summary.all_in_street !== null) {
    const last = frames[frames.length - 1];
    for (let count = Math.max(last.boardCount, 3); count <= hand.board.length; count += 1) {
      if (count <= last.boardCount) continue;
      frames.push({
        ...last,
        step: frames.length,
        street: count - 2,
        boardCount: count,
        event: null,
        decision: null,
        actor: null,
        runout: true,
      });
    }
  }
  return frames;
}

/*
 * What the bot did, in the fewest words that are still true. RAISE_TO names a
 * total street contribution, not an increment, so the "to" stays.
 *
 * A raise is named by how many bets deep it is, which is what the table talks
 * about -- a three-bet is a different thing from a bet, and calling both of
 * them "raise" hides the shape of the hand. Preflop the blind counts as the
 * first bet, so the opener's raise is the two-bet and the next is the
 * three-bet; postflop the first wager is the bet and the first raise of it is
 * the re-raise.
 */
export function raiseName(street: number, priorAggression: number) {
  if (street === 0) {
    const level = priorAggression + 2;
    return level === 2 ? 'raise' : `${level}-bet`;
  }
  if (priorAggression === 0) return 'bet';
  if (priorAggression === 1) return 're-raise';
  return `${priorAggression + 1}-bet`;
}

export function actionLabel(
  decision: HandDecision,
  bigBlind: number,
  priorAggression = 0,
) {
  const size = (chips: number) => `${(chips / bigBlind).toFixed(1)} BB`;
  switch (decision.applied.type) {
    case 1:
      return 'fold';
    case 2:
      return 'check';
    case 3:
      return `call ${size(decision.to_call)}`;
    case 4: {
      const name = raiseName(decision.street, priorAggression);
      const preposition = name === 'bet' ? '' : 'to ';
      return `${name} ${preposition}${size(decision.applied.amount_to)}`;
    }
    default:
      return 'act';
  }
}
