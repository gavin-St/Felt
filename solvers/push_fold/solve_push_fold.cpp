/*
 * 200bb heads-up shove-or-fold solver.
 *
 * Offline tool. Builds a 169x169 preflop equity matrix by Monte Carlo using the
 * vendored OMPEval, then finds an approximate equilibrium of the restricted
 * shove-or-fold game by fictitious play, and emits a C table for
 * bots/nash_push_fold.
 *
 * This solves a deliberately restricted game: both players may only shove or
 * fold preflop. That is not heads-up NLHE, and at 200 bb it is very far from it
 * -- the real game is played postflop. What it gives is the best a pure
 * shove-or-fold strategy can do at this depth, which is exactly what the bot
 * is meant to represent.
 *
 * Usage:  solve_push_fold [SAMPLES_PER_MATCHUP] [OUTPUT_HEADER]
 */

#include "omp/HandEvaluator.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

namespace {

const char* kRanks = "23456789TJQKA";

/* Card ids are rank-major, matching Felt and OMPEval: card = rank * 4 + suit. */
int card_of(int rank, int suit) { return rank * 4 + suit; }

/*
 * Hand-class index, 0..168. Pairs sit on the diagonal at rank*13+rank.
 * Suited hands are stored at low*13+high, offsuit at high*13+low.
 */
int class_index(int rank_a, int rank_b, bool suited) {
  const int high = rank_a > rank_b ? rank_a : rank_b;
  const int low = rank_a > rank_b ? rank_b : rank_a;
  if (rank_a == rank_b) {
    return high * 13 + low;
  }
  return suited ? low * 13 + high : high * 13 + low;
}

int class_weight(int index) {
  const int row = index / 13;
  const int col = index % 13;
  if (row == col) return 6;   /* pair */
  return row < col ? 4 : 12;  /* suited : offsuit */
}

/*
 * Grid cell to class index. Rows and columns both run A down to 2, so a cell
 * above the diagonal (row rank higher than column rank) is the suited one.
 */
int grid_index(int row_rank, int col_rank) {
  if (row_rank == col_rank) return row_rank * 13 + row_rank;
  return col_rank * 13 + row_rank;
}

std::uint64_t rng_state = 0x243F6A8885A308D3ULL;

std::uint64_t next_random() {
  std::uint64_t x = rng_state;
  x ^= x << 13U;
  x ^= x >> 7U;
  x ^= x << 17U;
  rng_state = x;
  return x;
}

int random_below(int bound) {
  return static_cast<int>(next_random() % static_cast<std::uint64_t>(bound));
}

void print_grid(const char* name, const bool* selected) {
  int combos = 0;
  for (int i = 0; i < 169; i++) {
    if (selected[i]) combos += class_weight(i);
  }
  std::printf("\n%s: %d/1326 combos = %.1f%%\n     ", name, combos,
              100.0 * combos / 1326.0);
  for (int c = 12; c >= 0; c--) std::printf(" %c ", kRanks[c]);
  std::printf("   (above the diagonal = suited)\n");
  for (int r = 12; r >= 0; r--) {
    std::printf("  %c  ", kRanks[r]);
    for (int c = 12; c >= 0; c--) {
      std::printf(" %s ", selected[grid_index(r, c)] ? "#" : ".");
    }
    std::printf("\n");
  }
}

}  // namespace

int main(int argc, char** argv) {
  const int samples = argc > 1 ? std::atoi(argv[1]) : 20000;
  const char* output_path = argc > 2 ? argv[2] : "push_fold_table.h";
  omp::HandEvaluator evaluator;

  /* Every concrete two-card combination of each class. */
  std::vector<std::vector<std::array<int, 2>>> combos(169);
  for (int ra = 0; ra < 13; ra++) {
    for (int rb = 0; rb < 13; rb++) {
      for (int sa = 0; sa < 4; sa++) {
        for (int sb = 0; sb < 4; sb++) {
          const int a = card_of(ra, sa);
          const int b = card_of(rb, sb);
          if (a >= b) continue;
          combos[class_index(ra, rb, sa == sb)].push_back({a, b});
        }
      }
    }
  }
  for (int i = 0; i < 169; i++) {
    if (static_cast<int>(combos[i].size()) != class_weight(i)) {
      std::fprintf(stderr, "combo count mismatch at class %d\n", i);
      return 1;
    }
  }

  /* Monte Carlo 169x169 equity, sampling disjoint combinations. */
  static float equity[169][169];
  int deck[52];
  std::fprintf(stderr, "equity matrix, %d samples per matchup...\n", samples);
  for (int a = 0; a < 169; a++) {
    for (int b = a; b < 169; b++) {
      double wins = 0.0;
      double ties = 0.0;
      int trials = 0;
      for (int s = 0; s < samples; s++) {
        const auto& ca = combos[a][random_below(static_cast<int>(combos[a].size()))];
        const auto& cb = combos[b][random_below(static_cast<int>(combos[b].size()))];
        if (ca[0] == cb[0] || ca[0] == cb[1] || ca[1] == cb[0] || ca[1] == cb[1]) {
          continue;  /* card removal: this pairing cannot occur */
        }
        int available = 0;
        for (int c = 0; c < 52; c++) {
          if (c == ca[0] || c == ca[1] || c == cb[0] || c == cb[1]) continue;
          deck[available++] = c;
        }
        for (int k = 0; k < 5; k++) {
          const int j = k + random_below(available - k);
          const int t = deck[k];
          deck[k] = deck[j];
          deck[j] = t;
        }
        omp::Hand board = omp::Hand::empty();
        for (int k = 0; k < 5; k++) board += omp::Hand(deck[k]);
        const unsigned va = evaluator.evaluate(board + omp::Hand(ca[0]) + omp::Hand(ca[1]));
        const unsigned vb = evaluator.evaluate(board + omp::Hand(cb[0]) + omp::Hand(cb[1]));
        if (va > vb) wins += 1.0;
        else if (va == vb) ties += 1.0;
        trials++;
      }
      const double e = trials > 0 ? (wins + 0.5 * ties) / trials : 0.5;
      equity[a][b] = static_cast<float>(e);
      equity[b][a] = static_cast<float>(1.0 - e);
    }
    if (a % 40 == 0) std::fprintf(stderr, "  row %d/169\n", a);
  }

  /*
   * Fictitious play. Iterated best response oscillates in this game, so each
   * side best-responds to the opponent's running average strategy instead.
   *
   * Payoffs in big blinds, from the acting player's perspective, relative to
   * the start of the hand. Stack S is the effective stack; the pot when both
   * are all in is 2S.
   *   SB folds                  -> -0.5   (the posted small blind)
   *   SB shoves, BB folds       -> +1.0   (the posted big blind)
   *   both all in               -> 2S * equity - S
   *   BB folds to a shove       -> -1.0
   */
  const double kStack = 200.0;
  const double kPot = 2.0 * kStack;
  std::vector<double> shove_frequency(169, 1.0);
  std::vector<double> call_frequency(169, 0.05);
  std::vector<double> shove_total(169, 0.0);
  std::vector<double> call_total(169, 0.0);
  const int kIterations = 20000;
  for (int t = 1; t <= kIterations; t++) {
    for (int c = 0; c < 169; c++) {
      double value = 0.0;
      double weight_total = 0.0;
      for (int d = 0; d < 169; d++) {
        const double w = class_weight(d);
        weight_total += w;
        value += w * (call_frequency[d] * (kPot * equity[c][d] - kStack) +
                      (1.0 - call_frequency[d]) * 1.0);
      }
      shove_total[c] += (value / weight_total) > -0.5 ? 1.0 : 0.0;
    }
    for (int d = 0; d < 169; d++) {
      double value = 0.0;
      double weight_total = 0.0;
      for (int c = 0; c < 169; c++) {
        const double w = class_weight(c) * shove_frequency[c];
        if (w <= 0.0) continue;
        weight_total += w;
        value += w * (kPot * equity[d][c] - kStack);
      }
      call_total[d] +=
          (weight_total > 0.0 && (value / weight_total) > -1.0) ? 1.0 : 0.0;
    }
    for (int i = 0; i < 169; i++) {
      shove_frequency[i] = shove_total[i] / t;
      call_frequency[i] = call_total[i] / t;
    }
  }

  bool sb_open[169];
  bool bb_call[169];
  int mixed = 0;
  for (int i = 0; i < 169; i++) {
    sb_open[i] = shove_frequency[i] >= 0.5;
    bb_call[i] = call_frequency[i] >= 0.5;
    if (shove_frequency[i] > 0.02 && shove_frequency[i] < 0.98) mixed++;
    if (call_frequency[i] > 0.02 && call_frequency[i] < 0.98) mixed++;
  }
  std::fprintf(stderr, "fictitious play: %d iterations, %d mixed classes\n",
               kIterations, mixed);

  /*
   * One derived spot: shoving over a limp. Bets faced are bucketed into two
   * cases, so anything at or below the big blind is the limp case, and any
   * raise is treated as facing a shove -- for which the equilibrium answer is
   * already bb_call, computed above. That keeps the bot's response to a raise
   * consistent with the solve instead of applying a second, stricter rule.
   */
  bool bb_vs_limp[169];
  for (int d = 0; d < 169; d++) {
    double value = 0.0;
    double weight_total = 0.0;
    for (int c = 0; c < 169; c++) {
      const double w = class_weight(c);
      weight_total += w;
      value += w * (bb_call[c] ? (kPot * equity[d][c] - kStack) : 1.0);
    }
    /* Shoving over a limp wins 1 bb when it folds out; checking is valued at 0. */
    bb_vs_limp[d] = (value / weight_total) > 0.0;
  }

  /*
   * Fourth spot, solved rather than approximated: shoving over a 2 bb raise.
   *
   * Unlike calling an all-in this one carries fold equity, so it needs its own
   * fixed point, and it needs an assumption about the raiser. Payoffs in bb:
   *   BB folds to the raise           -> -1
   *   BB shoves, raiser folds         -> +2   (the raiser's 2 bb)
   *   both all in                     -> 2S * equity - S
   *   raiser folds to the re-shove    -> -2
   * The raiser calls when 2S*e - S > -2, i.e. e > 0.495.
   *
   * The answer depends heavily on how wide the raiser is, so this reports it
   * across several assumed raising ranges rather than pretending one is right.
   */
  {
    /* Rank hands by equity against a uniformly random hand, for "top X%". */
    std::vector<std::pair<double, int>> ranked;
    for (int c = 0; c < 169; c++) {
      double value = 0.0;
      double weight_total = 0.0;
      for (int d = 0; d < 169; d++) {
        const double w = class_weight(d);
        weight_total += w;
        value += w * equity[c][d];
      }
      ranked.push_back({value / weight_total, c});
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const std::pair<double, int>& a, const std::pair<double, int>& b) {
                return a.first > b.first;
              });

    const int raise_percents[] = {100, 40, 20, 10};
    for (int rp : raise_percents) {
      bool raises[169] = {false};
      int budget = (1326 * rp) / 100;
      for (auto& entry : ranked) {
        if (budget <= 0) break;
        raises[entry.second] = true;
        budget -= class_weight(entry.second);
      }
      bool reshove[169];
      bool villain_calls[169];
      for (int i = 0; i < 169; i++) { reshove[i] = bb_call[i]; villain_calls[i] = bb_call[i]; }
      for (int iter = 0; iter < 200; iter++) {
        for (int v = 0; v < 169; v++) {
          if (!raises[v]) { villain_calls[v] = false; continue; }
          double value = 0.0, weight_total = 0.0;
          for (int d = 0; d < 169; d++) {
            if (!reshove[d]) continue;
            const double w = class_weight(d);
            weight_total += w;
            value += w * (kPot * equity[v][d] - kStack);
          }
          villain_calls[v] = weight_total > 0.0 && (value / weight_total) > -2.0;
        }
        for (int d = 0; d < 169; d++) {
          double value = 0.0, weight_total = 0.0;
          for (int v = 0; v < 169; v++) {
            if (!raises[v]) continue;
            const double w = class_weight(v);
            weight_total += w;
            value += w * (villain_calls[v] ? (kPot * equity[d][v] - kStack) : 2.0);
          }
          reshove[d] = weight_total > 0.0 && (value / weight_total) > -1.0;
        }
      }
      char title[96];
      std::snprintf(title, sizeof(title),
                    "BB re-shove over a 2bb raise (raiser opens top %d%%)", rp);
      print_grid(title, reshove);
    }
  }

  print_grid("SB open shove", sb_open);
  print_grid("BB call a shove", bb_call);
  print_grid("BB shove over a limp", bb_vs_limp);
  print_grid("BB continue versus a raise (= BB call a shove)", bb_call);

  std::FILE* out = std::fopen(output_path, "w");
  if (out == nullptr) {
    std::fprintf(stderr, "cannot write %s\n", output_path);
    return 1;
  }
  std::fprintf(out,
               "/* Generated by solvers/push_fold. Do not edit by hand. */\n"
               "/* 200 bb heads-up shove-or-fold equilibrium.            */\n"
               "/* %d Monte Carlo samples per matchup, %d iterations.    */\n\n"
               "#ifndef FELT_PUSH_FOLD_TABLE_H\n#define FELT_PUSH_FOLD_TABLE_H\n\n"
               "#define FELT_PF_SB_OPEN     1U  /* shove as SB */\n"
               "#define FELT_PF_BB_VS_LIMP  2U  /* shove over a limp as BB */\n"
               "#define FELT_PF_BB_VS_RAISE 4U  /* continue all-in facing a raise */\n\n"
               "/* Index: pairs at rank*13+rank, suited at low*13+high, "
               "offsuit at high*13+low. */\n"
               "static const unsigned char felt_push_fold[169] = {\n",
               samples, kIterations);
  for (int i = 0; i < 169; i++) {
    const unsigned value = (sb_open[i] ? 1U : 0U) | (bb_vs_limp[i] ? 2U : 0U) |
                           (bb_call[i] ? 4U : 0U);
    std::fprintf(out, "  %u,%s", value, (i % 13 == 12) ? "\n" : "");
  }
  std::fprintf(out, "};\n\n#endif\n");
  std::fclose(out);
  std::fprintf(stderr, "wrote %s\n", output_path);
  return 0;
}
