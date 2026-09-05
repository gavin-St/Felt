/**
 * Per-bot identity: colour, glyph, character, and the story each one tells.
 *
 * Colour is deliberately NOT the identifier. Eight categorical hues cannot be
 * told apart reliably — validated against this surface, the worst all-pairs
 * distance is well under the readable floor even with full colour vision, and
 * no reordering fixes it. So every appearance of a bot carries glyph + name,
 * with colour as reinforcement only. The hues are the documented categorical
 * palette in its fixed order; slots stay bound to bots, never to rank.
 */

export type BotProfile = {
  slug: string;
  character: string;
  tagline: string;
  color: string;
  glyph: GlyphName;
  /** One-line behaviour, for the matrix tooltip and the video lower third. */
  behaviour: string;
  /** The longer story — why it exists and what it demonstrates. */
  story: string[];
  /** 169 chars, row-major from A down to 2; above the diagonal is suited. */
  range?: string;
  rangeLabel?: string;
  stats: { label: string; value: string }[];
};

export type GlyphName =
  | 'burst'
  | 'descend'
  | 'lock'
  | 'peak'
  | 'target'
  | 'die'
  | 'wall'
  | 'flag';

const ALWAYS =
  '1111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111';
const NIT =
  '1000000000000010000000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000';
const BETTER =
  '1111111111111111111111111111111000000001111100000000111110000000011000100000001100000000000110000000000011000000000001100000000000110000000000011000000000001100000000000';
const WORSE =
  '0000000000000000000000000000000000000000000000000000000000000000000100000000000011000000000001110000000000111100000000011111000000001111110000000111111100000011111111000';
const SOLVED =
  '1111100001100110000000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000';

export const BOT_PROFILES: Record<string, BotProfile> = {
  'always-all-in': {
    slug: 'always-all-in',
    character: 'The Cannon',
    tagline: 'Every hand. Every time. No exceptions.',
    color: '#e34948',
    glyph: 'burst',
    behaviour: 'Shoves all 200 big blinds with any two cards.',
    story: [
      'The Cannon has exactly one move and makes it every hand from either seat, with any two cards. It never folds, never calls, never sees a flop it did not put its whole stack into.',
      'It exists to be an oracle. Paired against a bot that only ever folds, the outcome of every hand is known in advance, which is what makes it useful for proving the betting engine and the chip accounting are right.',
      'It is also the worst case for the equity code. Forcing an all-in before the flop every hand means enumerating 1,712,304 possible boards per new matchup, against 990 on the flop — roughly 1,700 times the work.',
    ],
    range: ALWAYS,
    rangeLabel: 'Shoves 1,326 of 1,326 combinations',
    stats: [
      { label: 'Hands shoved', value: '100%' },
      { label: 'Equity vs a random hand', value: '50.1%' },
      { label: 'Decisions per hand', value: '1' },
    ],
  },
  'check-call': {
    slug: 'check-call',
    character: 'The Anchor',
    tagline: 'Never folds. Never raises. Never learns.',
    color: '#4a3aa7',
    glyph: 'wall',
    behaviour: 'Calls any bet, checks when checking is free.',
    story: [
      'The Anchor pays off every value bet ever made against it and never wins a pot it did not simply have the best hand in. It is the purest calling station that can exist.',
      'Because it always sees every street a hand can reach, it is the opponent that exercises postflop play hardest — nothing ever ends early.',
      'It also doubles as the cheapest correctness check in the project. Against The Cannon it scores exactly zero, to the chip, raw and adjusted. Neither bot looks at its cards, so the two halves of every duplicate pair are perfect mirror images and cancel completely. Any other number would mean a bug in the dealing, the pairing, or the accounting.',
    ],
    stats: [
      { label: 'Folds', value: 'Never' },
      { label: 'Raises', value: 'Never' },
      { label: 'vs The Cannon', value: 'Exactly 0.00' },
    ],
  },
  'check-fold': {
    slug: 'check-fold',
    character: 'The Mouse',
    tagline: 'Surrenders to any resistance at all.',
    color: '#e87ba4',
    glyph: 'flag',
    behaviour: 'Checks when free, folds to any bet.',
    story: [
      'The Mouse never voluntarily puts a chip in the pot. If checking is free it checks; the moment anyone bets, it is gone.',
      'It is the floor of the whole reference set and the first opponent any new bot should face, because the result is entirely predictable. It surrenders the big blind almost every hand it is dealt one.',
      'Anything that cannot beat The Mouse comfortably is either emitting illegal actions or folding hands it was being given for free.',
    ],
    stats: [
      { label: 'Chips risked voluntarily', value: 'None' },
      { label: 'Wins only when', value: 'Opponent folds first' },
      { label: 'Rating', value: 'Last' },
    ],
  },
  'seeded-random': {
    slug: 'seeded-random',
    character: 'The Dice',
    tagline: 'A coin flip with a random amount attached.',
    color: '#2a78d6',
    glyph: 'die',
    behaviour: 'Picks uniformly among the legal actions, sizing at random.',
    story: [
      'The Dice chooses uniformly among whatever is legal. Facing a bet that is a clean third each to fold, call and raise; with no bet to face it is an even split between checking and raising. Raise sizes are uniform across the entire legal range, so it will min-raise and shove with equal enthusiasm.',
      'Every choice is derived from the per-decision randomness the harness supplies and nothing else, which makes it perfectly reproducible: replay the same situation and it makes the same decision.',
      'Its real job is reaching strange places. Scripted bots walk narrow paths; this one produces four-bet wars, tiny raises, short all-ins that fail to reopen the action, and every combination of streets — which is exactly what shakes out engine and logging bugs.',
    ],
    stats: [
      { label: 'Facing a bet', value: '33 / 33 / 33' },
      { label: 'No bet to face', value: '50 / 50' },
      { label: 'Raise sizing', value: 'Uniform, any legal amount' },
    ],
  },
  'nit-all-in': {
    slug: 'nit-all-in',
    character: 'The Vault',
    tagline: 'Aces, kings, queens. Nothing else is worth opening for.',
    color: '#eda100',
    glyph: 'lock',
    behaviour: 'Shoves only AA, KK and QQ — 1.4% of hands.',
    story: [
      'The Vault waits for a premium pair and shoves it. Everything else goes in the bin: 18 of 1,326 combinations, one hand in seventy.',
      'It is the folding extreme of the shove-or-fold family, and its results show exactly how expensive that is. Against The Cannon it wins the smallest margin of any bot in the set, because surrendering the blinds ninety-eight times out of a hundred bleeds away faster than the premiums bring back.',
      'The interesting part is that its instinct is nearly right for one specific decision. Facing an all-in at this depth, the solved calling range is aces, kings, queens and ace-king suited — almost exactly this. The Vault is wrong as an opening range, not as a calling one.',
    ],
    range: NIT,
    rangeLabel: 'Shoves 18 of 1,326 combinations (1.4%)',
    stats: [
      { label: 'Hands shoved', value: '1.4%' },
      { label: 'Equity vs a random hand', value: '82.4%' },
      { label: 'Combinations', value: '18 of 1,326' },
    ],
  },
  'better-all-in': {
    slug: 'better-all-in',
    character: 'The Bruiser',
    tagline: 'Any ace, any king, any two big cards. Send it.',
    color: '#eb6834',
    glyph: 'peak',
    behaviour: 'Shoves pairs 99+, any two broadway, any ace, any king — 34.1%.',
    story: [
      'The Bruiser shoves a third of all hands: every pocket pair from nines up, any two cards ten or higher, and — the clause that does most of the work — any ace or any king at all.',
      'Those last two dominate the rest. The full ace and king rows mean it is shoving king-deuce offsuit and ace-trey offsuit, which makes it far looser than the word "premium" suggests.',
      'It is also the most brutal bot in the set against pure aggression. A wide-but-selective range against a range of literally any two cards is an enormous edge, and it beats The Cannon by the largest margin anyone manages.',
    ],
    range: BETTER,
    rangeLabel: 'Shoves 452 of 1,326 combinations (34.1%)',
    stats: [
      { label: 'Hands shoved', value: '34.1%' },
      { label: 'Equity vs a random hand', value: '59.9%' },
      { label: 'Combinations', value: '452 of 1,326' },
    ],
  },
  'worse-all-in': {
    slug: 'worse-all-in',
    character: 'The Fool',
    tagline: 'Shoves the hands everyone else throws away.',
    color: '#1baf7a',
    glyph: 'descend',
    behaviour: 'Shoves only junk — offsuit, disconnected, no ace or king.',
    story: [
      'The Fool was built to be bad on purpose. Its range is the complement of everything worth playing: no pairs, nothing suited, no connectors or one-gappers, and no aces or kings. What survives is queen-nine offsuit down to seven-deuce offsuit.',
      'That range averages 44% equity against a single random hand. It is committing two hundred big blinds as an underdog to any two cards, before the opponent even gets to fold the worst of theirs.',
      'Its real value is as a controlled opposite of The Bruiser. The two shove almost exactly as often — 32.6% against 34.1% — but over completely disjoint ranges. Aggression is held constant and only hand selection differs, so the gap between them measures selection alone.',
    ],
    range: WORSE,
    rangeLabel: 'Shoves 432 of 1,326 combinations (32.6%)',
    stats: [
      { label: 'Hands shoved', value: '32.6%' },
      { label: 'Equity vs a random hand', value: '44.0%' },
      { label: 'Best hand in range', value: 'Q9 offsuit' },
    ],
  },
  'solved-all-in': {
    slug: 'solved-all-in',
    character: 'The Solver',
    tagline: 'The best a shove-or-fold strategy can possibly do.',
    color: '#008300',
    glyph: 'target',
    behaviour: 'Plays solved 200bb shove-or-fold ranges.',
    story: [
      'The Solver plays ranges computed rather than guessed — an approximate equilibrium of the game where both players may only shove or fold. Opening, it shoves 4.1% of hands. Facing an all-in it continues with aces, kings, queens and ace-king suited.',
      'The odd inclusions are real, not noise. Ace-five and ace-four suited sit alongside ace-ten because wheel aces pick up straight equity and block an ace-heavy calling range.',
      'Everything is tight because shoving risks two hundred big blinds to win one and a half. That needs either enormous equity or enormous fold equity, and at this depth neither is available often.',
      'It tops the ratings, but the way it wins is the lesson. It beats The Bruiser — yet The Bruiser beats The Cannon by more than six times the margin The Solver manages against the same opponent. These ranges are the correct answer to an opponent playing the solved range; against one shoving every hand, folding 98% of the time declines a coin flip it should take. Unexploitable is not the same as maximally exploitative.',
    ],
    range: SOLVED,
    rangeLabel: 'Opening shove: 54 of 1,326 combinations (4.1%)',
    stats: [
      { label: 'Opening shove', value: '4.1%' },
      { label: 'Calling an all-in', value: '1.7%' },
      { label: 'Rating', value: 'First' },
    ],
  },
};

export function botProfile(name: string): BotProfile | undefined {
  return BOT_PROFILES[name];
}

export const RANKS = 'AKQJT98765432';
