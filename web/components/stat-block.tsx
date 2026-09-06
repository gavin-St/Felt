import type { StatBlock } from '@/lib/dashboard';
import { signed } from '@/lib/dashboard';

/* One decimal reads fine until a rate is genuinely tiny -- 7 all-ins in 20,000
 * hands is not the same as none at all, so small values keep two more. */
function percent(value: number | null) {
  if (value === null) return '—';
  if (value !== 0 && Math.abs(value) < 0.1) return `${value.toFixed(3)}%`;
  return `${value.toFixed(1)}%`;
}

function tone(value: number | null) {
  if (value === null) return 'text-[#8b8177]';
  return value >= 0 ? 'text-[#087343]' : 'text-[#b52d24]';
}

/* A chip result the snapshot does not carry yet reads as a dash, never as
 * zero: an unmeasured result and a break-even one are not the same claim. */
function chips(value: number | null) {
  return value === null ? '—' : `${signed(value, 1)} BB`;
}

/*
 * The headline row is four chip results in big blinds. Preflop, postflop
 * non-showdown, and showdown add up to raw. The second row is rate stats.
 */
export function StatBlockView({ stats }: { stats: StatBlock }) {
  const headline: Array<[string, number | null, string]> = [
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
      `${percent(stats.preflopShare)} of hands`,
    ],
    [
      'Postflop non-showdown',
      stats.postflopNonshowdownBb,
      `${percent(stats.postflopNonshowdownShare)} of hands`,
    ],
    [
      'Showdown result',
      stats.showdownBb,
      `${percent(stats.showdownShare)} of hands`,
    ],
  ];

  const secondary: Array<[string, string]> = [
    ['VPIP / PFR', `${percent(stats.vpip)} / ${percent(stats.pfr)}`],
    ['Aggression frequency', percent(stats.aggression)],
    ['C-bet %', percent(stats.cbet)],
    [
      'Average pot size',
      stats.averagePotBb === null ? '—' : `${stats.averagePotBb.toFixed(1)} BB`,
    ],
    ['Showdown % (WTSD)', percent(stats.wtsd)],
    ['Won at showdown', percent(stats.wsd)],
  ];

  return (
    <>
      <div className="grid gap-3 sm:grid-cols-2 lg:grid-cols-4">
        {headline.map(([label, value, note]) => (
          <div
            key={label}
            className="min-h-28 border border-[#cfc4b6] bg-[#fffdf8] p-5"
          >
            <span className="block text-xs uppercase tracking-[.08em] text-[#756a60]">
              {label}
            </span>
            <strong className={`mt-3 block font-mono text-2xl ${tone(value)}`}>
              {chips(value)}
            </strong>
            <span className="mt-2 block text-xs text-[#8b8177]">{note}</span>
          </div>
        ))}
      </div>

      <div className="mt-3 grid grid-cols-2 gap-2 sm:grid-cols-3 lg:grid-cols-6">
        {secondary.map(([label, value]) => (
          <div
            key={label}
            className="border border-[#ded5c9] bg-[#fbf8f1] px-3 py-3"
          >
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
