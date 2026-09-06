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

/*
 * The headline row: three chip results and, in place of the fourth, where the
 * hands actually ended. Each result leads with bb per hand, because that is
 * the number that compares across matchups of different lengths, and carries
 * the total underneath. All three per-hand rates use every hand as the
 * denominator, so preflop plus postflop plus showdown adds back up to the
 * overall win rate.
 */
type Result = {
  label: string;
  perHand: number | null;
  total: number | null;
};

/* The three shares are a part-to-whole, so they take one ink ramp rather than
 * the win/loss hues -- ending a hand early is not good or bad in itself. */
const DISTRIBUTION_INK = ['#241f1b', '#8b8177', '#cfc4b6'];

function Distribution({ stats }: { stats: StatBlock }) {
  const parts = [
    ['Preflop', stats.preflopShare],
    ['Postflop', stats.postflopNonshowdownShare],
    ['Showdown', stats.showdownShare],
  ] as const;
  const known = parts.every(([, value]) => value !== null);
  return (
    <div className="min-h-28 border border-[#cfc4b6] bg-[#fffdf8] p-5">
      <span className="block text-xs uppercase tracking-[.08em] text-[#756a60]">
        Where hands end
      </span>
      {known ? (
        <>
          <div className="mt-3 flex h-1.5 w-full overflow-hidden rounded-full">
            {parts.map(([label, value], index) => (
              <span
                key={label}
                style={{
                  width: `${value}%`,
                  background: DISTRIBUTION_INK[index],
                }}
              />
            ))}
          </div>
          <dl className="mt-2.5 space-y-0.5">
            {parts.map(([label, value], index) => (
              <div key={label} className="flex items-baseline gap-2">
                <span
                  aria-hidden
                  className="h-2 w-2 shrink-0 rounded-full"
                  style={{ background: DISTRIBUTION_INK[index] }}
                />
                <dt className="text-xs text-[#756a60]">{label}</dt>
                <dd className="ml-auto font-mono text-xs text-[#241f1b]">
                  {percent(value)}
                </dd>
              </div>
            ))}
          </dl>
        </>
      ) : (
        <strong className="mt-3 block font-mono text-2xl text-[#8b8177]">—</strong>
      )}
    </div>
  );
}

function ResultCell({ label, perHand, total }: Result) {
  return (
    <div className="min-h-28 border border-[#cfc4b6] bg-[#fffdf8] p-5">
      <span className="block text-xs uppercase tracking-[.08em] text-[#756a60]">
        {label}
      </span>
      <strong className="mt-3 flex items-baseline gap-1.5 font-mono text-2xl">
        <span className={tone(perHand)}>
          {perHand === null ? '—' : signed(perHand)}
        </span>
        <span className="text-[11px] font-normal text-[#8b8177]">
          bb / hand
        </span>
      </strong>
      <span className="mt-2 block text-xs text-[#8b8177]">
        {total === null ? '—' : `${signed(total, 1)} BB total`}
      </span>
    </div>
  );
}

export function StatBlockView({ stats }: { stats: StatBlock }) {
  const results: Result[] = [
    { label: 'Raw result', perHand: stats.bbPerHand, total: stats.rawBb },
    {
      label: 'Postflop non-showdown',
      perHand: stats.postflopNonshowdownBbPerHand,
      total: stats.postflopNonshowdownBb,
    },
    {
      label: 'Showdown result',
      perHand: stats.showdownBbPerHand,
      total: stats.showdownBb,
    },
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
        <ResultCell {...results[0]} />
        <Distribution stats={stats} />
        <ResultCell {...results[1]} />
        <ResultCell {...results[2]} />
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
