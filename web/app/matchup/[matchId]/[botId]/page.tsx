import Link from 'next/link';
import { notFound } from 'next/navigation';

import { StatBlockView } from '@/components/stat-block';
import { dashboard, signed, statBlock } from '@/lib/dashboard';

type PageProps = {
  params: Promise<{ matchId: string; botId: string }>;
};

export default async function MatchupPage({ params }: PageProps) {
  const route = await params;
  const match = dashboard.matches.find((item) => item.id === Number(route.matchId));
  if (!match) notFound();

  const player =
    match.players.find((item) => item.bot_id === Number(route.botId)) ?? match.players[0];
  const opponent = match.players.find((item) => item.bot_id !== player.bot_id);
  if (!opponent) notFound();

  const tone = (chips: number) => (chips >= 0 ? 'text-[#087343]' : 'text-[#b52d24]');
  const stats = statBlock([{ player, bigBlind: match.big_blind }]);

  const buckets = [...player.buckets].sort(
    (left, right) => right.adjusted_net_chips - left.adjusted_net_chips,
  );

  const bucketTable = (items: typeof buckets, heading: string) => (
    <table className="w-full border-collapse border border-[#cfc4b6] bg-[#fffdf8] text-sm">
      <thead>
        <tr>
          <th className="border-b border-[#e3dbd0] p-3 text-left">{heading}</th>
          <th className="border-b border-[#e3dbd0] p-3 text-right">Hands</th>
          <th className="border-b border-[#e3dbd0] p-3 text-right">Total</th>
          <th className="border-b border-[#e3dbd0] p-3 text-right">BB / hand</th>
        </tr>
      </thead>
      <tbody>
        {items.map((bucket) => (
          <tr key={bucket.bucket}>
            <td className="border-b border-[#e3dbd0] p-3">{bucket.bucket}</td>
            <td className="border-b border-[#e3dbd0] p-3 text-right">{bucket.hands}</td>
            <td
              className={`border-b border-[#e3dbd0] p-3 text-right ${tone(bucket.adjusted_net_chips)}`}
            >
              {signed(bucket.adjusted_net_chips / match.big_blind)} BB
            </td>
            <td className="border-b border-[#e3dbd0] p-3 text-right">
              {signed(bucket.adjusted_bb_per_hand)}
            </td>
          </tr>
        ))}
      </tbody>
    </table>
  );

  return (
    <main className="min-h-screen bg-[#f6f2e9] text-[#241f1b]">
      <div className="mx-auto max-w-[1180px] px-6 py-8 pb-20">
        <header className="flex items-center justify-between border-b border-[#bdb2a6] pb-6">
          <Link href="/" className="font-semibold hover:underline">← Matchup matrix</Link>
          <span className="font-mono text-xs uppercase tracking-[.08em]">
            {match.hand_count.toLocaleString()} hands · seed {match.match_seed}
          </span>
        </header>

        <section className="grid items-center gap-6 py-11 md:grid-cols-[1fr_auto_1fr]">
          <div>
            <p className="font-mono text-xs uppercase tracking-[.08em]">Hero bot</p>
            <h1 className="mt-1 font-serif text-4xl">
              <Link href={`/bot/${player.bot_id}`} className="hover:underline">
                {player.bot_name}
              </Link>
            </h1>
            <p className="mt-2 text-sm">
              {player.wins.toLocaleString()} wins · {player.losses.toLocaleString()} losses
            </p>
          </div>
          <div className="md:text-center">
            <strong className={`font-mono text-3xl ${tone(player.adjusted_bb_per_hand)}`}>
              {signed(player.adjusted_bb_per_hand)}
            </strong>
            <span className="mt-1 block text-sm text-[#756a60]">adjusted bb / hand</span>
          </div>
          <div className="md:text-right">
            <p className="font-mono text-xs uppercase tracking-[.08em]">Opponent</p>
            <h2 className="mt-1 font-serif text-4xl">
              <Link href={`/bot/${opponent.bot_id}`} className="hover:underline">
                {opponent.bot_name}
              </Link>
            </h2>
            <p className="mt-2 text-sm">
              {opponent.wins.toLocaleString()} wins · {opponent.losses.toLocaleString()} losses
            </p>
          </div>
        </section>

        <section>
          <StatBlockView stats={stats} />
        </section>

        <p className="mt-3 text-xs text-[#8b8177]">
          Showdown and non-showdown split the raw result in two; the preflop figure is the
          part of the non-showdown line that never reached a flop.
        </p>

        <section className="mt-9">
          <h2 className="mb-4 font-serif text-2xl">{player.bot_name} starting hands</h2>
          <div className="grid gap-5 md:grid-cols-2">
            {bucketTable(buckets.slice(0, 8), 'Most profitable')}
            {bucketTable(buckets.slice(-8).reverse(), 'Least profitable')}
          </div>
        </section>
      </div>
    </main>
  );
}
