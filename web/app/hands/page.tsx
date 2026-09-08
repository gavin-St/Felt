import { notFound } from 'next/navigation';

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
  if (!HAND_REPLAY_ENABLED) notFound();
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
