import { Link } from 'react-router';

import { HandSearch } from '@/components/hand-search';
import { useTitle } from '@/lib/title';

export default function HandsPage() {
  useTitle('Hand search — Felt');
  return (
    <main className="min-h-screen bg-[#f6f2e9] text-[#241f1b]">
      <div className="mx-auto max-w-[1180px] px-6 py-8 pb-20">
        <HandSearch />
      </div>
    </main>
  );
}

/*
 * What the published site shows instead. The search runs against a local
 * SQLite server holding the whole ledger, which is not something a folder of
 * static files can carry -- but every matchup links here, so the page exists
 * and says where the ledger lives rather than 404ing.
 */
export function Unavailable() {
  return (
    <main className="min-h-screen bg-[#f6f2e9] text-[#241f1b]">
      <div className="mx-auto max-w-[680px] px-6 py-16">
        <Link to="/" className="font-mono text-xs text-[#756b60] underline">
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
