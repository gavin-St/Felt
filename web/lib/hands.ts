/*
 * The hand ledger, unlike everything else on this site, is not in the bundle.
 *
 * `dashboard.ts` imports its snapshot at build time, which is why the matchup
 * matrix needs no server at all. That cannot scale to 4.62 million hands, and
 * the published site is a folder of static files, so there is nothing there to
 * query even if the database were shipped. The replay browser therefore talks to
 * `scripts/hand_server.py`, which runs on the machine that has the database.
 *
 * So the feature is local by construction. In `pnpm dev` it is on; in a
 * production build import.meta.env.DEV is false, the replay routes are never
 * registered, and the published site says so rather than breaking.
 * Setting `globalThis.FELT_HAND_API` in the console overrides the address for
 * the rare case of pointing a local page at a server on another port.
 */

export const HAND_REPLAY_ENABLED = import.meta.env.DEV;

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
  | 'all-in-preflop'
  | 'three-bet'
  | 'four-bet'
  | 'big-pot'
  | 'won'
  | 'lost'
  | 'cbet'
  | 'hero-bluff'
  | 'opponent-bluff'
  | 'in-position'
  | 'out-of-position'
  | 'opponent-folded'
  | 'hero-folded';

/*
 * Label, what the filter actually tests -- several are easy to read as
 * something slightly different from what the SQL asks -- and the group it
 * belongs to. Filters in a group are alternatives, not ingredients: a hand
 * cannot both end before the flop and be shown down, and asking for both
 * returns nothing while looking like a narrower question. Selecting one hides
 * its siblings until it is cleared.
 *
 * The order is the order they are offered in: the street a hand reached
 * first, because that is the coarsest cut, then how big it got, then
 * everything else.
 */
export type HandFilterGroup =
  | 'street'
  | 'pot-type'
  | 'position'
  | 'result'
  | 'folder';

export const HAND_FILTERS: Array<
  [HandFilter, string, string, HandFilterGroup | null]
> = [
  ['preflop', 'Ended preflop', 'Never saw a flop', 'street'],
  ['postflop', 'Saw a flop', 'Reached the flop, shown down or not', 'street'],
  ['postflop-no-showdown', 'Flop, no showdown', 'Saw a flop, then folded', 'street'],
  ['showdown', 'Showdown', 'Both hands were shown', 'street'],
  ['big-pot', 'Big pot', 'Final pot 40 BB or more', null],
  ['all-in', 'All-in', 'Stacks went in', null],
  ['all-in-preflop', 'All-in preflop', 'Stacks went in before the flop', null],
  ['three-bet', '3-bet+', 'Three-bet or bigger preflop', 'pot-type'],
  ['four-bet', '4-bet+', 'Four-bet or bigger preflop', 'pot-type'],
  ['won', 'Hero won', 'Positive result for the hero bot', 'result'],
  ['lost', 'Hero lost', 'Negative result for the hero bot', 'result'],
  ['cbet', 'Hero c-bet', 'Hero bet the flop as preflop raiser', null],
  ['hero-bluff', 'Hero bluffed', 'Hero bet or raised after the flop as a bluff', null],
  ['opponent-bluff', 'Opponent bluffed', 'The other bot bet or raised after the flop as a bluff', null],
  ['in-position', 'In position', 'Hero on the button, last after the flop', 'position'],
  ['out-of-position', 'Out of position', 'Hero in the big blind, first after the flop', 'position'],
  ['opponent-folded', 'Opponent folded', 'The other bot gave it up', 'folder'],
  ['hero-folded', 'Hero folded', 'The hero bot gave it up', 'folder'],
];

/* The alternatives to a filter, itself excluded. */
export function filterSiblings(filter: HandFilter): HandFilter[] {
  const group = HAND_FILTERS.find(([name]) => name === filter)?.[3];
  if (!group) return [];
  return HAND_FILTERS.filter(
    ([name, , , other]) => other === group && name !== filter,
  ).map(([name]) => name);
}

/* Drop anything that contradicts an earlier choice, for filters arriving from
 * a URL or a saved search rather than from a click. */
export function consistentFilters(filters: HandFilter[]): HandFilter[] {
  const kept: HandFilter[] = [];
  for (const filter of filters) {
    if (kept.some((other) => filterSiblings(other).includes(filter))) continue;
    kept.push(filter);
  }
  return kept;
}

/*
 * Deal order is the default: the hands as they were played, which is the one
 * ordering that means something on its own and that reads the same way every
 * time. Shuffled is a stable permutation rather than a fresh one -- the key is
 * a hash of the match and the hand index, not a die roll -- but it is stable
 * only within a ledger, since rerunning a matchup changes the match key and so
 * changes the shuffle. Deal order survives that.
 */
export const DEFAULT_HAND_SORT = 'played';

export const HAND_SORTS: Array<[string, string]> = [
  ['played', 'Deal order'],
  ['pot', 'Biggest pot'],
  ['won-most', 'Hero won most'],
  ['lost-most', 'Hero lost most'],
  ['random', 'Shuffled'],
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
  /* Set by the server: this bet or raise was a bluff, either because the bot
   * said so or because the holding was weak two pair or worse. */
  bluff?: boolean;
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

/* --- the published sample ---------------------------------------------- */

/*
 * The ledger is eight gigabytes and its compressed hand logs alone are 1.2 GB,
 * so the published site carries two hundred hands per match instead: about
 * 42 kB gzipped each, twenty-two megabytes in total, written by
 * web/scripts/export_hand_sample.py whenever matches are published.
 *
 * Everything below reproduces what hand_server.py does in SQL -- the same
 * filters, the same orders, the same totals -- over those rows. It is only
 * reached when the local server does not answer, so a machine with the ledger
 * never touches it, and the two can be told apart by handSource().
 */
export type HandSource = 'live' | 'sample' | 'unknown';

let source: HandSource = 'unknown';
let sampleSize = 0;

export function handSource(): HandSource {
  return source;
}

/** Hands per match in the published sample, once meta has been read. */
export function handSampleSize(): number {
  return sampleSize;
}

type SampleSummary = HandSummary & {
  all_in_street: number | null;
  bluffed: number;
  opponent_bluffed: number;
};

type SampleFile = {
  match_id: number;
  hand_count: number;
  sampled: number;
  summaries: SampleSummary[];
  hands: Record<string, HandDetail>;
};

function sampleUrl(name: string) {
  return `${import.meta.env.BASE_URL}data/hands/${name}.json.gz`;
}

/*
 * The files are gzipped in the repository because twenty-two megabytes is
 * worth carrying and two hundred is not.
 *
 * Whether they arrive compressed depends on the host: some serve a .gz as
 * application/gzip and hand over the bytes, others set Content-Encoding and
 * the browser has already inflated it. Rather than guess, read the bytes and
 * look -- 1f 8b is the gzip magic number, and nothing else starts a JSON
 * document.
 */
async function readGzip<T>(url: string): Promise<T> {
  const response = await fetch(url);
  if (!response.ok) throw new Error(`${response.status} for ${url}`);
  const bytes = new Uint8Array(await response.arrayBuffer());
  const compressed = bytes[0] === 0x1f && bytes[1] === 0x8b;
  if (!compressed) {
    return JSON.parse(new TextDecoder().decode(bytes)) as T;
  }
  if (typeof DecompressionStream === 'undefined') {
    throw new Error('this browser cannot inflate the published hand sample');
  }
  const stream = new Blob([bytes as BlobPart])
    .stream()
    .pipeThrough(new DecompressionStream('gzip'));
  return JSON.parse(await new Response(stream).text()) as T;
}

const files = new Map<number, Promise<SampleFile>>();

function sampleFile(matchId: number): Promise<SampleFile> {
  const known = files.get(matchId);
  if (known) return known;
  const pending = readGzip<SampleFile>(sampleUrl(String(matchId)));
  files.set(matchId, pending);
  return pending;
}

let sampleMeta: Promise<HandMeta> | null = null;

function sampleMetaOnce(): Promise<HandMeta> {
  sampleMeta ??= readGzip<HandMeta & { sample?: number }>(sampleUrl('meta')).then(
    (value) => {
      sampleSize = value.sample ?? 0;
      return value;
    },
  );
  return sampleMeta;
}

/* The SQL in hand_server.py's FILTERS, one predicate each. */
const SAMPLE_FILTERS: Record<HandFilter, (row: SampleSummary) => boolean> = {
  showdown: (r) => r.showdown === 1,
  postflop: (r) => r.saw_flop === 1,
  'postflop-no-showdown': (r) => r.saw_flop === 1 && r.showdown === 0,
  preflop: (r) => r.saw_flop === 0,
  'all-in': (r) => r.all_in_reached === 1,
  'all-in-preflop': (r) => r.all_in_reached === 1 && r.all_in_street === 0,
  'three-bet': (r) =>
    r.pot_class === 'three_bet' || r.pot_class === 'four_bet_plus',
  'four-bet': (r) => r.pot_class === 'four_bet_plus',
  'big-pot': (r) => r.final_pot_chips >= 4000,
  won: (r) => r.outcome === 'win',
  lost: (r) => r.outcome === 'loss',
  cbet: (r) => r.cbet_made === 1,
  'hero-bluff': (r) => r.bluffed === 1,
  'opponent-bluff': (r) => r.opponent_bluffed === 1,
  'in-position': (r) => r.position === 0,
  'out-of-position': (r) => r.position === 1,
  'opponent-folded': (r) => r.end_reason === 1 && r.folded_position !== r.position,
  'hero-folded': (r) => r.end_reason === 1 && r.folded_position === r.position,
};

const SAMPLE_SORTS: Record<string, (a: SampleSummary, b: SampleSummary) => number> = {
  played: (a, b) => a.match_id - b.match_id || a.hand_index - b.hand_index,
  pot: (a, b) => b.final_pot_chips - a.final_pot_chips,
  'won-most': (a, b) => b.raw_net_chips - a.raw_net_chips,
  'lost-most': (a, b) => a.raw_net_chips - b.raw_net_chips,
  /* The sample was drawn in the ledger's own shuffled order, so the order it
   * arrives in is already that shuffle, narrowed. */
  random: () => 0,
};

async function sampleSearch(query: {
  bot?: number;
  opponent?: number;
  match?: number;
  hand?: string;
  filters?: HandFilter[];
  sort?: string;
  limit?: number;
  offset?: number;
}) {
  const meta = await sampleMetaOnce();
  const ids = new Set<number>();
  for (const row of meta.matchups) {
    if (query.match && row.match_id !== query.match) continue;
    const heroSide = query.bot === undefined || row.bot_id === query.bot ||
      row.opponent_bot_id === query.bot;
    const otherSide = query.opponent === undefined ||
      row.bot_id === query.opponent || row.opponent_bot_id === query.opponent;
    if (heroSide && otherSide) ids.add(row.match_id);
  }
  const loaded = await Promise.all([...ids].map(sampleFile));

  const wanted = (query.hand ?? '').trim().toUpperCase();
  const predicates = (query.filters ?? []).map((name) => SAMPLE_FILTERS[name]);
  const rows: SampleSummary[] = [];
  for (const file of loaded) {
    for (const row of file.summaries) {
      if (query.bot !== undefined && row.bot_id !== query.bot) continue;
      if (query.opponent !== undefined && row.opponent_bot_id !== query.opponent) {
        continue;
      }
      if (wanted && row.bucket.toUpperCase() !== wanted) continue;
      if (predicates.some((match) => match && !match(row))) continue;
      rows.push(row);
    }
  }
  const order = SAMPLE_SORTS[query.sort ?? 'random'] ?? SAMPLE_SORTS.random;
  rows.sort(order);

  const summary: HandSummaryTotals = {
    hands: rows.length,
    raw_net_chips: 0,
    adjusted_net_chips: 0,
    wins: 0,
    showdowns: 0,
    showdown_wins: 0,
    pot_chips: 0,
    saw_flop: 0,
  };
  for (const row of rows) {
    summary.raw_net_chips += row.raw_net_chips;
    summary.adjusted_net_chips += row.adjusted_net_chips;
    summary.wins += row.outcome === 'win' ? 1 : 0;
    summary.showdowns += row.showdown;
    summary.showdown_wins += row.showdown_win;
    summary.pot_chips += row.final_pot_chips;
    summary.saw_flop += row.saw_flop;
  }

  const offset = query.offset ?? 0;
  const limit = query.limit ?? 20;
  return {
    total: rows.length,
    limit,
    offset,
    summary,
    hands: rows.slice(offset, offset + limit) as HandSummary[],
  };
}

/*
 * The local server first, always. It has every hand; the sample has two
 * hundred per match. Which one answered is remembered so the page can say so.
 */
export async function fetchHandMeta(): Promise<HandMeta> {
  try {
    const live = await get<HandMeta>('/api/meta');
    source = 'live';
    return live;
  } catch {
    const sampled = await sampleMetaOnce();
    source = 'sample';
    return sampled;
  }
}

export type HandSummaryTotals = {
  hands: number;
  raw_net_chips: number;
  adjusted_net_chips: number;
  wins: number;
  showdowns: number;
  showdown_wins: number;
  pot_chips: number;
  saw_flop: number;
};

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
  if (source === 'sample') return sampleSearch(query);
  return get<{
    total: number;
    limit: number;
    offset: number;
    summary: HandSummaryTotals;
    hands: HandSummary[];
  }>(`/api/hands?${search}`);
}

export async function fetchHand(
  matchId: number,
  handIndex: number,
): Promise<HandDetail> {
  if (source !== 'sample') {
    try {
      const live = await get<HandDetail>(`/api/hand/${matchId}/${handIndex}`);
      source = 'live';
      return live;
    } catch (error) {
      /* A replay opened directly, with no search to have found the server
       * missing first. Fall through to the sample and remember. */
      if (source === 'live') throw error;
    }
  }
  const file = await sampleFile(matchId);
  source = 'sample';
  const hand = file.hands[String(handIndex)];
  if (!hand) {
    throw new Error(
      `hand ${handIndex} is not in the published sample of match ${matchId}`,
    );
  }
  return hand;
}

/* --- shared vocabulary ------------------------------------------------- */

export const STREETS = ['Preflop', 'Flop', 'Turn', 'River'];

/*
 * Named the way the filter chips name them, and shared so the two cannot
 * drift. A hand in the three_bet class is exactly a three-bet, so it is a
 * "3-bet" here where the chip is "3-bet+" -- the chip means three-bet or
 * bigger. four_bet_plus really does lump everything above it, so it keeps
 * the plus.
 */
export const POT_CLASS_LABELS: Record<string, string> = {
  walk: 'Walk',
  limped_unraised: 'Limped',
  single_raised: 'Single raised',
  three_bet: '3-bet',
  four_bet_plus: '4-bet+',
};

export const potClassLabel = (potClass: string) =>
  POT_CLASS_LABELS[potClass] ?? potClass.replace(/_/g, ' ');

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

/*
 * A four-colour deck, the way the big sites draw one: spades black, hearts
 * red, diamonds blue, clubs green. Two black suits are hard to tell apart at
 * the size these cards are drawn, and a misread suit is a misread hand.
 */
export const SUIT_COLORS: Record<string, string> = {
  s: '#241f1b',
  h: '#b52d24',
  d: '#1d63b8',
  c: '#0a7040',
};

/*
 * ActionViolation from harness/include/felt/hand_engine.hpp. A bot that runs
 * past the decision cap is not making a bad play, it is making no play: the
 * harness checks or folds for it. That is worth saying differently from a
 * request the rules refused.
 */
export const TIMED_OUT = 5;

export function violationNote(violation: number): string | null {
  if (violation === 0) return null;
  return violation === TIMED_OUT
    ? 'Out of time; the harness checked or folded for it.'
    : 'Illegal request; the harness substituted the action above.';
}

export function suitColor(card: string): string {
  return SUIT_COLORS[suitOf(card)] ?? SUIT_COLORS.s;
}

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
