import type { StatBlock } from '@/lib/dashboard';
import { signed } from '@/lib/dashboard';

function percent(value: number | null) {
  return value === null ? '—' : `${value.toFixed(1)}%`;
}

function tone(value: number) {
  return value >= 0 ? 'text-[#087343]' : 'text-[#b52d24]';
}

/*
 * The headline row is four chip results in big blinds; showdown and
 * non-showdown add up to raw, and preflop is the slice of non-showdown that
 * never reached a flop. The second row is rate stats, deliberately quieter.
 */
export function StatBlockView({ stats }: { stats: StatBlock }) {
  const headline: Array<[string, number, string]> = [
    [
      'Raw result',
      stats.rawBb,
      stats.bbPerHand === null
        ? `${stats.hands.toLocaleString()} hands`
        : `${signed(stats.bbPerHand)} bb / hand`,
    ],
    [
      'Preflop result',
      stats.preflopBb,
      `${stats.preflopHands.toLocaleString()} hands ended preflop`,
    ],
    ['Showdown result', stats.showdownBb, `${percent(stats.showdownShare)} of hands`],
    [
      'Non-showdown result',
      stats.nonshowdownBb,
      `${percent(stats.nonshowdownShare)} of hands`,
    ],
  ];

  const secondary: Array<[string, string]> = [
    ['VPIP / PFR', `${percent(stats.vpip)} / ${percent(stats.pfr)}`],
    ['Aggression frequency', percent(stats.aggression)],
    ['C-bet %', percent(stats.cbet)],
    ['Showdown % (WTSD)', percent(stats.wtsd)],
    ['All-in reached', percent(stats.allInReached)],
    ['Won at showdown', percent(stats.wsd)],
  ];

  return (
    <>
      <div className="grid gap-3 sm:grid-cols-2 lg:grid-cols-4">
        {headline.map(([label, value, note]) => (
          <div key={label} className="min-h-28 border border-[#cfc4b6] bg-[#fffdf8] p-5">
            <span className="block text-xs uppercase tracking-[.08em] text-[#756a60]">
              {label}
            </span>
            <strong className={`mt-3 block font-mono text-2xl ${tone(value)}`}>
              {signed(value, 1)} BB
            </strong>
            <span className="mt-2 block text-xs text-[#8b8177]">{note}</span>
          </div>
        ))}
      </div>

      <div className="mt-3 grid grid-cols-2 gap-2 sm:grid-cols-3 lg:grid-cols-6">
        {secondary.map(([label, value]) => (
          <div key={label} className="border border-[#ded5c9] bg-[#fbf8f1] px-3 py-3">
            <span className="block text-[10px] uppercase tracking-[.07em] text-[#8b8177]">
              {label}
            </span>
            <strong className="mt-1.5 block font-mono text-base font-normal text-[#4a423b]">
              {value}
            </strong>
          </div>
        ))}
      </div>
    </>
  );
}
