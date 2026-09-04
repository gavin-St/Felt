# Prior Art

Two projects have already built roughly what this one builds. Neither is a
competitor — one is finished, the other solves a different problem — but both
made design choices worth copying or deliberately rejecting.

---

## Annual Computer Poker Competition (ACPC), 2006–2018

The academic benchmark for computer poker. Ran annually for thirteen years, then
stopped — not from failure, but because its central problem was solved.

**Timeline:**

| Year | Event |
|---|---|
| 2008 | Polaris (Alberta) beats human professionals at heads-up **limit** hold'em |
| 2015 | Cepheus **weakly solves** heads-up limit hold'em — unbeatable over a human lifetime |
| 2017 | DeepStack and Libratus independently reach **superhuman** heads-up no-limit; Libratus beats four top HUNL specialists over 120,000 hands |
| 2018 | Final competition |
| 2019 | Pluribus beats elite pros at 6-max; frontier moves to multiway |

### How much compute the human-beating bots actually used

Worth knowing, because it sets the scale this harness deliberately does *not*
operate at.

| | Libratus (CMU, 2017) | DeepStack (Alberta, 2016-17) |
|---|---|---|
| Hardware | PSC *Bridges* supercomputer, ~600 nodes during play | **a single GTX 1080** |
| Offline compute | **19 million core-hours** | far less; a trained network |
| Time per decision | real-time subgame solving on ~600 nodes | **median 2.3 s** (0.04 s preflop, 5.9 s flop, 5.4 s turn) |
| Evaluation | 120,000 hands, 20 days, 4 pros | 44,852 hands, 33 players, ~5 weeks |

**On the 120,000 hands:** it was not one long match. Four pros played in
parallel, 30,000 hands each over 20 days — roughly 1,500 hands per pro per day —
and pairs of players were dealt *mirrored* hands, the same duplicate-poker
variance reduction this harness uses. DeepStack got its sample differently:
online, 33 players at 3,000 hands each, with players multi-tabling.

DeepStack's median 2.3 s per action is about **1,150x** our 2 ms cap. That gap is
the point: this harness is built for fast iteration on cheap bots, not for
hosting a research-scale solver. The `--decision-cap-ms 500 --hands 3000` mode in
SPEC.md exists for anything approaching that class.

The competition's own scale was also large: over 70 million hands across the 2012
event.

### What we inherit from it

The game itself, unchanged and deliberately so — 50/100 blinds, 20,000 stacks,
reset every hand (*Doyle's Game*), duplicate deals with seats swapped, and all-in
equity adjustment. Keeping these exact means results are comparable to published
computer-poker work for free.

### Where we diverge

| | ACPC | This project |
|---|---|---|
| Transport | TCP socket protocol | direct `dlopen` call |
| Bot language | any | C ABI (C++ wrappers allowed) |
| Time per decision | seconds | 2 ms default |
| Compute | supercomputer-scale | one trusted, single-threaded bot call |
| Bot state across hands | **allowed and central** | **forbidden** |
| Match length | millions of hands | 40,000 |

Two of these matter more than the rest.

**Statelessness is the big one.** Opponent modeling — watching a rival over
thousands of hands and adapting — was a main ACPC research thread. Forbidding it
means this harness measures *how strong is this strategy in a vacuum*, not *how
well does this bot learn to beat that bot*. That is a defensible narrowing
(cleaner results, no ordering effects, far cheaper) but it excludes an entire
category of poker AI. Say so plainly in the bot-author guide, so nobody arrives
with an exploiter and wonders why it is hobbled.

**The direct ABI buys speed and costs isolation and languages.** A C function
call has effectively no transport overhead, but a bad bot can crash or hang the
match and Python cannot participate directly. This is the right v1 tradeoff for
trusted local bots. A future process protocol can add isolation and other
languages without changing the poker engine.

---

## MIT Pokerbots (course 6.9630), annual, still running

A one-month MIT IAP competition where student teams of 1–4 build a bot from
scratch. Backed by quant trading firms (Hudson River Trading, Jump, Citadel,
Jane Street, Two Sigma) with $50k+ in prizes — it is as much a recruiting
pipeline as a research exercise, which explains its emphasis on fast onboarding
over research depth.

**Engine specifics** (from their reference implementation's config):

| Setting | Value |
|---|---|
| Hands per match | 1,000 |
| Starting stack | 400, blinds 1/2 (**200 bb** — same depth as ACPC and ours) |
| Time limit | **30 s game clock for the whole match**, not per decision |
| Build timeout | 10 s |
| Connect timeout | 10 s |
| Languages | Python, Java, C++ via skeleton bots over a socket protocol |
| Engine language | Python, using `eval7` for hand evaluation |
| Per-bot log cap | 512 KB |

That three independent designs — ACPC, MIT, and this one — all landed on **200 bb
effective** is a decent signal that the depth is right.

### What to take from it

1. **Their single match clock prompted us to drop ours.** MIT gives each bot one
   30 s budget for 1,000 hands to spend as it likes. Felt instead measures a
   fixed CPU cap per decision, after the direct call returns. There is no time
   bank to hoard or spend. Recoverable hard timeouts require process isolation
   and are deliberately deferred.

2. **Build and connect failures.** Their engine handles a bot that fails to
   start, not just one that misbehaves once running. Felt treats `dlopen`
   failure, missing functions, and ABI mismatch as setup errors rather than
   match forfeits.

3. **A per-bot log cap.** They bound what a bot can write to disk. Felt v1 trusts
   bots and does not attempt this; an untrusted runner will need bounded output
   and filesystem restrictions.

4. **A fresh, secret game variant every year.** Unveiled only at kickoff, which
   prevents teams from downloading pre-solved GTO output and forces actual
   algorithm work. Not directly applicable to a personal harness, but the
   structural lesson is worth heeding: avoid wiring Hold'em assumptions so deep
   that a variant becomes impossible. We are hard-wired to NLHE by choice, which
   is fine — just make it a known cost, not an accident.

5. **Short matches, many of them.** 1,000 hands per match versus our 40,000, with
   volume coming from a large tournament bracket instead. See the note below on
   why match length matters more than it looks.

---

## Statistical power — read this before building the Elo tool

40,000 hands can be coarser than it sounds. As an illustrative planning case,
suppose duplicate play plus all-in equity adjustment produces a 5 bb standard
deviation per hand-equivalent. Then:

```
SE(bb/100) = 100 * sigma / sqrt(N)
           = 100 * 5 / sqrt(40000)
           = 2.5 bb/100
```

So a default match resolves differences of about **5 bb/100** at two sigma, and
no finer. Distinguishing two strong bots that genuinely differ by 1 bb/100 would
need roughly **1,000,000 hands**.

The 5 bb assumption is not a guarantee. Variance reduction depends strongly on
the two strategies: duplicate halves can cancel nearly perfectly when their
action paths match, while equity adjustment matters only when a hand reaches an
all-in before the river. Felt should report raw and adjusted uncertainty from
duplicate-pair totals so the reduction is measured for each matchup.

That is fine for *is this change an improvement* and poor for *rank these six
similar bots*. It will make Elo ratings jittery between closely-matched
opponents, so the Elo tool must carry uncertainty per rating rather than
reporting a point estimate.

The known remedy is **AIVAT**, the variance-reduction method Alberta developed
after the ACPC, which uses a baseline value function to strip out luck and
reportedly cuts variance by roughly an order of magnitude — it is what made
DeepStack's evaluation viable at 44k hands. It needs a reference strategy and is
genuinely complex, so it is not an M0–M10 item. It is the right answer if match
precision ever becomes the binding constraint.

---

## Sources

- [Computer poker player — ACPC history and results](https://handwiki.org/wiki/Software:Computer_poker_player)
- [DeepStack supplementary materials — ACPC HUNL rules](https://poker.cs.ualberta.ca/publications/17science-supplementary.pdf)
- [Libratus (IJCAI 2017)](https://www.ijcai.org/proceedings/2017/0772.pdf)
- [Annual Computer Poker Competition](http://www.computerpokercompetition.org/)
- [MIT Pokerbots](https://pokerbots.org/)
- [MIT Pokerbots reference engine](https://github.com/mitpokerbots/engine)
