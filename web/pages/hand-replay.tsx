import { Link, useLocation } from 'react-router';
import { useParams } from 'react-router';

import { HandReplay } from '@/components/hand-replay';
import { NotFound } from '@/pages/not-found';

/* Registered only in development -- see routes.tsx -- so there is no check for
 * the ledger here, only for an address that does not name a hand. */
export default function HandReplayPage() {
  const location = useLocation();
  const remembered = (location.state as { searchUrl?: unknown } | null)?.searchUrl;
  const searchUrl = typeof remembered === 'string' && remembered.startsWith('/hands?')
    ? remembered : '/hands';
  const route = useParams<{ matchId: string; handIndex: string }>();
  const matchId = Number(route.matchId);
  const handIndex = Number(route.handIndex);
  if (!Number.isInteger(matchId) || !Number.isInteger(handIndex)) {
    return <NotFound what="hand" />;
  }

  return (
    <main className="min-h-screen bg-[#f6f2e9] text-[#241f1b]">
      <div className="mx-auto max-w-[1180px] px-6 py-8 pb-20">
        <header className="mb-8 flex items-center justify-between border-b border-[#bdb2a6] pb-6">
          <Link to={searchUrl} className="font-semibold hover:underline">
            ← Hand search
          </Link>
          <span className="font-mono text-xs uppercase tracking-[.08em] text-[#756a60]">
            match {matchId} · hand {handIndex.toLocaleString()}
          </span>
        </header>
        <HandReplay matchId={matchId} handIndex={handIndex} />
      </div>
    </main>
  );
}
