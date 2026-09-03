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

Scale was serious: over 70 million hands across the 2012 competition, and
Libratus consumed on the order of 25M core-hours.

### What we inherit from it

The game itself, unchanged and deliberately so — 50/100 blinds, 20,000 stacks,
reset every hand (*Doyle's Game*), duplicate deals with seats swapped, and all-in
equity adjustment. Keeping these exact means results are comparable to published
computer-poker work for free.

### Where we diverge

| | ACPC | This project |
|---|---|---|
| Transport | TCP socket protocol | `dlopen` + shared memory |
| Bot language | any | C/C++ ABI only |
| Time per decision | seconds | 2 ms default |
| Compute | supercomputer-scale | 1 core, 1 GB |
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

**The transport buys speed and costs languages.** Microsecond IPC suits a 2 ms
cap in a way millisecond sockets never could, but it locks bots to C++. The
escape hatch, if a Python bot is ever wanted, is a thin `.so` that proxies over a
socket — worth leaving room for, not worth building now.

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

1. **The single match clock is simpler than our three-layer timing.** MIT gives
   each bot one 30 s budget for 1,000 hands and lets it spend that however it
   likes. We specify a 2 ms per-decision soft cap *plus* a 10 s match bank *plus*
   a 200 ms hard ceiling. Ours does buy something theirs does not — the soft cap
   enforces a *shape* of bot, preventing one from dumping its whole budget into a
   single hand — but it is worth asking at M5 whether the middle layer earns its
   complexity, or whether a match clock plus a hard ceiling would do.

2. **Build and connect timeouts.** Their engine handles a bot that fails to
   *start*, not just one that misbehaves once running. Our spec covers runtime
   violations but says nothing about `dlopen` failure, a missing `create_bot`
   symbol, or an ABI version mismatch. Added to M3.

3. **A per-bot log cap.** They bound what a bot can write to disk. We bound the
   harness's own logging but not a bot's.

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

40,000 hands is coarser than it sounds.

Heads-up no-limit runs roughly **10 bb standard deviation per hand**. Duplicate
poker plus all-in equity adjustment cuts that to perhaps 5 bb. Then:

```
SE(bb/100) = 100 * sigma / sqrt(N)
           = 100 * 5 / sqrt(40000)
           = 2.5 bb/100
```

So a default match resolves differences of about **5 bb/100** at two sigma, and
no finer. Distinguishing two strong bots that genuinely differ by 1 bb/100 would
need roughly **1,000,000 hands**.

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
