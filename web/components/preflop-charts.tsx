import Link from 'next/link';

import {
  preflopActionStyles,
  preflopCharts,
  type PreflopAction,
  type PreflopChart,
} from '@/lib/preflop';

const actionNames: Record<PreflopAction, string> = {
  fold: 'Fold',
  passive: 'Check / limp / call',
  value: 'Raise',
  bluff: 'Bluff raise',
  'all-in': 'All-in',
};

function ChartGrid({ chart }: { chart: PreflopChart }) {
  const counts = chart.cells.reduce<Record<PreflopAction, number>>(
    (total, cell) => {
      total[cell.action] +=
        cell.hand.length === 2 ? 6 : cell.hand.endsWith('s') ? 4 : 12;
      return total;
    },
    { fold: 0, passive: 0, value: 0, bluff: 0, 'all-in': 0 },
  );

  return (
    <article className="border border-[#332d27] bg-[#fffdf8] shadow-[6px_6px_0_#d9d0c3]">
      <header className="border-b border-[#d8cfc2] px-4 py-4 sm:px-5">
        <p className="font-mono text-xs font-semibold tracking-[0.08em] text-[#b42c23]">
          {chart.context}
        </p>
        <h2 className="mt-1 font-serif text-2xl font-medium tracking-tight">
          {chart.title}
        </h2>
      </header>

      <div className="overflow-x-auto p-3 sm:p-4">
        <div className="min-w-[520px]">
          <table
            className="w-full table-fixed border-separate border-spacing-1"
            aria-label={chart.title}
          >
            <tbody>
              {Array.from({ length: 13 }, (_, row) => (
                <tr key={row}>
                  {chart.cells.slice(row * 13, row * 13 + 13).map((cell) => {
                    const readableAction =
                      cell.action === 'passive'
                        ? chart.passiveLabel
                        : actionNames[cell.action];
                    return (
                      <td key={cell.hand} className="p-0">
                        <div
                          title={`${cell.hand}: ${readableAction}`}
                          style={preflopActionStyles[cell.action]}
                          className="flex aspect-square items-center justify-center rounded-[2px] border font-mono text-[11px] font-semibold"
                        >
                          <span className="sr-only">{readableAction}: </span>
                          {cell.hand}
                        </div>
                      </td>
                    );
                  })}
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </div>

      <footer className="flex flex-wrap gap-x-4 gap-y-1 border-t border-[#e0d8cc] px-4 py-3 font-mono text-[11px] text-[#756b60] sm:px-5">
        {(['value', 'bluff', 'all-in', 'passive', 'fold'] as PreflopAction[])
          .filter((action) => counts[action] > 0)
          .map((action) => (
            <span key={action}>
              {action === 'passive' ? chart.passiveLabel : actionNames[action]}{' '}
              {counts[action]}
            </span>
          ))}
      </footer>
    </article>
  );
}

export function PreflopCharts() {
  return (
    <main className="min-h-screen bg-[#f6f2e9] text-[#231f1b]">
      <div className="mx-auto max-w-[1500px] px-5 py-7 sm:px-8">
        <header className="mb-7 border-b-4 border-[#27221e] pb-5">
          <Link
            href="/"
            className="font-mono text-xs font-semibold uppercase tracking-[0.08em] text-[#756b60] hover:text-[#b42c23] focus-visible:outline-2 focus-visible:outline-[#bf2f25]"
          >
            ← Head-to-head scorecard
          </Link>
          <h1 className="mt-4 font-serif text-4xl font-medium tracking-tight sm:text-5xl">
            Preflop charts
          </h1>
          <p className="mt-2 text-sm text-[#695f55]">
            baseline_100bb_v1 · heads-up
          </p>
        </header>

        <section
          aria-label="Action legend"
          className="mb-7 flex flex-wrap gap-x-5 gap-y-2"
        >
          {(
            ['value', 'bluff', 'passive', 'all-in', 'fold'] as PreflopAction[]
          ).map((action) => (
            <div key={action} className="flex items-center gap-2 text-sm">
              <span
                className="size-3 border"
                style={preflopActionStyles[action]}
                aria-hidden="true"
              />
              <span>{actionNames[action]}</span>
            </div>
          ))}
        </section>

        <section className="grid gap-7 xl:grid-cols-2">
          {preflopCharts.map((chart) => (
            <ChartGrid key={chart.id} chart={chart} />
          ))}
        </section>

        <section className="mt-9 border-y-2 border-[#332d27] bg-[#eee7dc] px-5 py-5 sm:flex sm:items-start sm:justify-between sm:gap-10">
          <div>
            <p className="font-mono text-xs font-semibold tracking-[0.08em] text-[#b42c23]">
              INTENTIONAL CONTROL
            </p>
            <h2 className="mt-1 font-serif text-2xl font-medium">
              action_count_v0
            </h2>
          </div>
          <p className="mt-3 max-w-3xl text-sm leading-6 text-[#554d45] sm:mt-0">
            The deliberately wrong version ignores the amount and routes the
            first raise to the small chart, the second to medium, and every
            later raise to large. A 200 bb opening shove is therefore treated
            exactly like an ordinary small open.
          </p>
        </section>
      </div>
    </main>
  );
}
