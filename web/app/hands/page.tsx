import Link from 'next/link';

import { HandSearch } from '@/components/hand-search';
import {
  HAND_REPLAY_ENABLED,
  consistentFilters,
  type HandFilter,
} from '@/lib/hands';

type PageProps = {
  searchParams: Promise<{
    bot?: string;
    opponent?: string;
    hand?: string;
    filters?: string;
    sort?: string;
    offset?: string;
    from?: string;
  }>;
};

export default async function HandsPage({ searchParams }: PageProps) {
  /*
   * The search runs against a local SQLite server holding the whole ledger,
   * which is not something the published site can carry. Rather than 404 --
   * every matchup links here -- the page exists and says where the replay
   * lives. The return happens before searchParams is read, which is also what
   * keeps this route static enough to export.
   */
  if (!HAND_REPLAY_ENABLED) return <Unavailable />;
  const query = await searchParams;
  const bot = Number(query.bot);
  const opponent = Number(query.opponent);
  const offset = Number(query.offset);
  const filters = consistentFilters(
    (query.filters ?? '')
      .split(',')
      .map((item) => item.trim())
      .filter(Boolean) as HandFilter[],
  );
  return (
    <main className="min-h-screen bg-[#f6f2e9] text-[#241f1b]">
      <div className="mx-auto max-w-[1180px] px-6 py-8 pb-20">
        <HandSearch
          initialBot={Number.isInteger(bot) && bot > 0 ? bot : undefined}
          initialOpponent={
            Number.isInteger(opponent) && opponent > 0 ? opponent : undefined
          }
          initialHand={query.hand}
          initialFilters={filters.length > 0 ? filters : undefined}
          initialSort={query.sort}
          initialOffset={Number.isInteger(offset) && offset > 0 ? offset : undefined}
          initialFrom={
            query.from && query.from.startsWith('/') ? query.from : undefined
          }
        />
      </div>
    </main>
  );
}

function Unavailable() {
  return (
    <main className="min-h-screen bg-[#f6f2e9] text-[#241f1b]">
      <div className="mx-auto max-w-[680px] px-6 py-16">
        <Link href="/" className="font-mono text-xs text-[#756b60] underline">
          &larr; Matchup matrix
        </Link>
        <h1 className="mt-8 font-serif text-3xl">Hand search</h1>
        <p className="mt-4 leading-relaxed text-[#4a423a]">
          Every hand of every match is kept in one SQLite ledger, and searching
          it means querying that file directly. It is several gigabytes, so it
          stays on the machine that ran the matches rather than being published
          alongside these pages.
        </p>
        <p className="mt-4 leading-relaxed text-[#4a423a]">
          Running the harness locally brings this page to life: the search, the
          filters and the hand-by-hand replay all read from the ledger on disk.
        </p>
      </div>
    </main>
  );
}
