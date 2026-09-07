import Link from 'next/link';
import { notFound } from 'next/navigation';

import { HandReplay } from '@/components/hand-replay';
import { HAND_REPLAY_ENABLED } from '@/lib/hands';

type PageProps = {
  params: Promise<{ matchId: string; handIndex: string }>;
};

export default async function HandReplayPage({ params }: PageProps) {
  if (!HAND_REPLAY_ENABLED) notFound();
  const route = await params;
  const matchId = Number(route.matchId);
  const handIndex = Number(route.handIndex);
  if (!Number.isInteger(matchId) || !Number.isInteger(handIndex)) notFound();

  return (
    <main className="min-h-screen bg-[#f6f2e9] text-[#241f1b]">
      <div className="mx-auto max-w-[1180px] px-6 py-8 pb-20">
        <header className="mb-8 flex items-center justify-between border-b border-[#bdb2a6] pb-6">
          <Link href="/hands" className="font-semibold hover:underline">
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
