import Link from 'next/link';
import { notFound } from 'next/navigation';

import { HandSearch } from '@/components/hand-search';
import { HAND_REPLAY_ENABLED } from '@/lib/hands';

type PageProps = {
  searchParams: Promise<{ bot?: string; opponent?: string; hand?: string }>;
};

export default async function HandsPage({ searchParams }: PageProps) {
  if (!HAND_REPLAY_ENABLED) notFound();
  const query = await searchParams;
  const bot = Number(query.bot);
  const opponent = Number(query.opponent);
  return (
    <main className="min-h-screen bg-[#f6f2e9] text-[#241f1b]">
      <div className="mx-auto max-w-[1180px] px-6 py-8 pb-20">
        <header className="flex items-center justify-between border-b border-[#bdb2a6] pb-6">
          <Link href="/" className="font-semibold hover:underline">
            ← Matchup matrix
          </Link>
          <span className="font-mono text-xs uppercase tracking-[.08em] text-[#756a60]">
            local only · reads data/felt.sqlite3
          </span>
        </header>
        <section className="py-9">
          <h1 className="font-serif text-4xl">Hand replay</h1>
          <p className="mt-2 max-w-[70ch] text-sm text-[#5c534b]">
            Every hand of every match, straight out of the ledger rather than the
            bundled snapshot. Pick a bot, narrow it down, and open one to step
            through it action by action.
          </p>
        </section>
        <HandSearch
          initialBot={Number.isInteger(bot) && bot > 0 ? bot : undefined}
          initialOpponent={
            Number.isInteger(opponent) && opponent > 0 ? opponent : undefined
          }
          initialHand={query.hand}
        />
      </div>
    </main>
  );
}
