# Game, dealing, and randomness rules

This document pins the rules that must be identical across implementations and
runs. The harness is the final authority on legality, cards, and randomness.

## Cards

Cards are bytes `0..51` in rank-major order:

```text
rank = card >> 2       // 0=2, ..., 8=T, 9=J, 10=Q, 11=K, 12=A
suit = card & 3        // 0=clubs, 1=diamonds, 2=spades, 3=hearts
```

Thus `2c = 0` and `Ah = 51`. The invalid-card value is `255`.

Felt uses ordinary 52-card heads-up no-limit Hold'em with no jokers, ante, rake,
or burn cards.

## Seats, blinds, and action order

- Position 0 is the button and posts the small blind.
- Position 1 posts the big blind.
- The button acts first preflop.
- The big blind acts first on the flop, turn, and river.
- After the button calls the big blind preflop, the big blind still has the
  option to check or raise.
- The default starting stack is 20,000 chips with 50/100 blinds: 200 big blinds.
- The CLI may configure another stack and blind size, but both players always
  start equally deep and stacks reset before every hand.

Blind posts count toward preflop street contributions but are not voluntary
actions for statistics.

## Betting

The pot always includes all chips committed so far, including unmatched current
bets. `my_stack` and `opp_stack` are chips remaining behind. Street
contributions reset to zero when a new street begins.

The minimum opening bet is one big blind. A full raise must increase the current
highest street contribution by at least the size of the previous full bet or
raise. Preflop, the big blind establishes the initial full-bet size.

A player may go all-in for less than a full raise when the player has not lost
the right to raise. This changes the amount to call but does not update the last
full-raise size and does not reopen raising for a player who has already acted
since the last full raise.

A betting round ends when:

- both players have acted since the last full aggressive action and their
  contributions are equal;
- a player folds; or
- one player is all-in and the other has called or folded.

If both remain and neither is all-in, advance to the next street. If no further
betting is possible, deal the remaining board immediately.

Equal starting stacks mean neither player can commit more total chips than the
other can match, so side pots and calls for less cannot occur. The engine should
enforce the effective-stack cap instead of accepting an unmatched excess bet.

## Showdown and pots

- A fold awards the entire pot to the other player.
- At showdown, the best five-card poker hand from seven cards wins.
- A tied pot is split evenly, with an odd chip awarded to the BB.
- Net winnings are `payout - chips_committed` for each player and sum to zero.

If both players are all-in before the river and equity adjustment is enabled,
enumerate every legal remaining board:

- preflop: `C(48,5) = 1,712,304` boards;
- flop: `C(45,2) = 990` boards;
- turn: `44` rivers.

For each position, equity is `(2*wins + ties) / (2*boards)`. Perform the
calculation with integer counts and a wide intermediate type. Set the BTN payout
to the floor of its rational expected payout and give the rest of the integer pot
to the BB. Still deal and log the seeded runout as the raw result.

Preflop equity-cache keys canonicalize the unordered pair of exact hole-card
combos. Reversing the lookup reverses wins A/B. There are 812,175 unordered,
disjoint exact-combo matchups before any suit-isomorphism reduction.

## Duplicate pairs

With duplicate play enabled, pair `k` is hands `2k` and `2k+1`.

1. Generate one shuffled deck for pair `k`.
2. Cards are attached to positions, not bot identities.
3. In hand `2k`, bot A occupies BTN/SB and bot B occupies BB.
4. In hand `2k+1`, the bots swap positions while both positional hole cards and
   the board remain identical.

This gives each bot both sides of the same deal. The `--hands` value counts
played hands, not unique decks, and must therefore be even in duplicate mode.

Without duplication, the button alternates every hand and every hand receives a
new deck.

## Shuffle and seed derivation

Reproducibility must not depend on `std::shuffle` or a standard-library random
distribution, whose exact output can differ by implementation.

The stable v1 construction is:

1. Encode integer inputs as unsigned 64-bit little-endian values.
2. Compute SHA-256 over the exact ASCII domain-label bytes (without a terminating
   null) followed by those inputs.
3. Interpret the digest as four little-endian 64-bit words to initialize
   `xoshiro256++`.
4. Shuffle a sorted `0..51` deck with Fisher-Yates from index 51 down to 1.
5. Choose each index with rejection sampling, not modulo reduction, so the
   bounded selection is unbiased.

Use the published xoshiro256++ 1.0 transition:

```text
result = rotl(s0 + s3, 23) + s0
t = s1 << 17
s2 ^= s0
s3 ^= s1
s1 ^= s2
s0 ^= s3
s2 ^= t
s3 = rotl(s3, 45)
```

All operations use wrapping unsigned 64-bit arithmetic. If the SHA-256 digest is
the all-zero state, replace `s0` with 1.

For a bound `b` in `1..52`, bounded sampling is exactly:

```text
threshold = (-b) mod b       // unsigned 64-bit arithmetic
repeat r = next_u64() until r >= threshold
return r mod b
```

Deal state uses the domain `felt/deal/v1` and inputs `(match_seed, deal_index)`,
where `deal_index` is the pair index in duplicate mode and the hand index
otherwise.

The physical deal from the shuffled deck is:

```text
0: BTN first card       1: BB first card
2: BTN second card      3: BB second card
4,5,6: flop             7: turn             8: river
```

Future board cards and the opponent's cards are never copied into a bot's game
state.

## Bot randomness

Each decision receives one unsigned 64-bit seed:

```text
little_endian_u64(first_8_bytes(SHA-256(
  "felt/bot-rng/v1" || match_seed || randomness_index ||
  decision_index || acting_position
)))
```

`randomness_index` is the pair index in duplicate mode and the hand index
otherwise. Both copies of a duplicate therefore use common random numbers for
the same position and decision ordinal. This makes identical stateless bots
produce an exactly symmetric duplicate pair and generally reduces random-strategy
variance. `decision_index` counts all decisions in that hand starting at zero.

Bots must derive all randomized choices from this value and must not use the wall
clock, OS randomness, or persistent PRNG state.

The match seed is an unsigned 64-bit integer. If `--seed` is omitted, the
harness generates one with macOS `arc4random_buf` and records it in
`summary.json` and every hand log.

The domain separation prevents an accidentally reused RNG stream from coupling
bot choices to the deck. It is not a security boundary: v1 bots run in-process
and are trusted.
