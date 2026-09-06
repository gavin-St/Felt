/**
 * Per-bot identity: colour, glyph, character, and the story each one tells.
 *
 * Colour is deliberately NOT the identifier. Thirteen categorical hues cannot be
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
  | 'flag'
  | 'shield'
  | 'flame'
  | 'split'
  | 'key'
  | 'probe';

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
      'It remains the highest-rated shove-only bot, but the way it wins is the lesson. It beats The Bruiser — yet The Bruiser beats The Cannon by more than six times the margin The Solver manages against the same opponent. These ranges are the correct answer to an opponent playing the solved range; against one shoving every hand, folding 98% of the time declines a coin flip it should take. Unexploitable is not the same as maximally exploitative.',
    ],
    range: SOLVED,
    rangeLabel: 'Opening shove: 54 of 1,326 combinations (4.1%)',
    stats: [
      { label: 'Opening shove', value: '4.1%' },
      { label: 'Calling an all-in', value: '1.7%' },
      { label: 'Rating', value: 'Best shove-only' },
    ],
  },
  'slp-fold': {
    slug: 'slp-fold',
    character: 'The Sentry',
    tagline: 'Strong hands advance. Air goes no farther.',
    color: '#536f91',
    glyph: 'shield',
    behaviour: 'Uses the shared preflop chart, then gives up whenever air meets resistance.',
    story: [
      'The Sentry is the first complete street-by-street strategy in Felt. It uses the shared 100 big blind chart before the flop, then sorts every postflop hand into three simple groups: top pair or better, a smaller pair or live draw, and air.',
      'Top pair or better bets three quarters of the pot and reraises to three times the opponent’s wager. Smaller pairs and live draws check or call. Air checks when free and folds to a bet; a missed river draw is air because there are no cards left to come.',
      'That restraint is an enormous weapon against The Spark. The Sentry wins 19.16 big blinds per hand in their direct match because it never joins the bluffing war without a real hand, while The Spark keeps escalating with nothing.',
    ],
    stats: [
      { label: 'Air bluffed', value: '0%' },
      { label: 'Value threshold', value: 'Top pair+' },
      { label: 'vs slp-bluff', value: '+19.16' },
    ],
  },
  'slp-bluff': {
    slug: 'slp-bluff',
    character: 'The Spark',
    tagline: 'If it finds air, it starts a fire.',
    color: '#bd3f78',
    glyph: 'flame',
    behaviour: 'Plays the shared strategy but bets or reraises every air hand.',
    story: [
      'The Spark is identical to The Sentry before the flop and with every made hand or draw. One switch changes: air is always aggressive. Checked to, it bets 75% of the pot; facing a bet, it reraises to three times the wager or goes all-in when there is not enough stack left.',
      'That makes it a deliberately pure over-bluffer. It can punish opponents that surrender too often, but it never learns when the story has stopped working. Two air hands can keep reraising one another until all 200 big blinds are in the middle.',
      'The controlled comparison exposes the cost clearly. It loses 19.16 big blinds per hand to The Sentry, despite sharing the same preflop chart, made-hand rules, draw rules, and bet sizes. The only experimental variable is what happens with air.',
    ],
    stats: [
      { label: 'Air bluffed', value: '100%' },
      { label: 'Bet / reraise', value: '75% / 3×' },
      { label: 'vs slp-fold', value: '−19.16' },
    ],
  },
  'slp-balance': {
    slug: 'slp-balance',
    character: 'The Switch',
    tagline: 'Every air hand reaches the same fifty-fifty fork.',
    color: '#7957a8',
    glyph: 'split',
    behaviour: 'Bluffs half its air decisions and gives up the other half.',
    story: [
      'The Switch sits exactly between the two pure air policies. It uses the same shared preflop chart, bets top pair or better, and checks or calls smaller pairs and live draws. When it holds air, the harness’s deterministic per-decision randomness chooses bluff or give-up with equal probability.',
      'The choice is reproducible rather than remembered. Replaying the same decision produces the same branch, and duplicate deals give the same positional decision the same random value. The bot therefore mixes without carrying hidden state between hands.',
      'Half as much bluffing is a large improvement against the always-bluff extreme—it wins 12.04 big blinds per hand against The Spark—but it still loses 4.87 to The Sentry. A 50% frequency is a useful experiment, not a claim that the frequency is balanced in a poker-theory sense.',
    ],
    stats: [
      { label: 'Air bluffed', value: '50%' },
      { label: 'Randomness', value: 'Deterministic' },
      { label: 'vs slp-bluff', value: '+12.04' },
    ],
  },
  'slp-exploit-fold': {
    slug: 'slp-exploit-fold',
    character: 'The Lockpick',
    tagline: 'Bluff the folders. Believe them when they fight back.',
    color: '#008b99',
    glyph: 'key',
    behaviour: 'Attacks air relentlessly, then continues versus aggression only with an overpair or better.',
    story: [
      'The Lockpick is the first opponent-specific exploit. It knows The Sentry folds air to every bet, so whenever action is checked over it attacks with the same always-bluff policy as The Spark.',
      'It also trusts the information The Sentry gives away. A postflop bet or raise from that profile means top pair or better, so The Lockpick folds everything below an overpair. With an overpair, two pair, or better it reraises to three times the wager or goes all-in when shorter.',
      'Those two adjustments beat The Sentry by 2.39 big blinds per hand. The lesson is not that these rules are generally strong—they are intentionally brittle—but that a predictable opponent can be beaten by a strategy designed around its exact leaks.',
    ],
    stats: [
      { label: 'Air attacked', value: '100%' },
      { label: 'Continue threshold', value: 'Overpair+' },
      { label: 'vs slp-fold', value: '+2.39' },
    ],
  },
  'slp-exploit-solved': {
    slug: 'slp-exploit-solved',
    character: 'The Probe',
    tagline: 'Apply the minimum pressure. Retreat at the exact boundary.',
    color: '#9a5d18',
    glyph: 'probe',
    behaviour: 'Open-min-raises every hand into the solved shove-or-fold policy, then calls only its narrow solved ranges.',
    story: [
      'The Probe targets The Solver’s deepest structural weakness: it has no ordinary call. A minimum raise with any two cards forces The Solver to choose between folding and risking its entire two-hundred-big-blind stack, so almost its whole range surrenders immediately.',
      'When The Solver open-shoves from the button, The Probe calls with the solved large-raise response: aces, kings, queens, and ace-king suited. When The Solver instead shoves over The Probe’s minimum raise, its range is only aces and kings, so The Probe tightens all the way to aces.',
      'If a flop appears, The Probe becomes the same postflop counter as The Lockpick. It keeps the full 75% bluff size when checked to and believes aggression unless it holds an overpair or better. That combination beat The Solver by 0.72 adjusted big blinds per hand in its first 20,000-hand match.',
    ],
    stats: [
      { label: 'Opening range', value: '100%' },
      { label: 'Open size', value: '2 bb' },
      { label: 'vs solved-all-in', value: '+0.72' },
    ],
  },
};

export function botProfile(name: string): BotProfile | undefined {
  return BOT_PROFILES[name];
}

export const RANKS = 'AKQJT98765432';
