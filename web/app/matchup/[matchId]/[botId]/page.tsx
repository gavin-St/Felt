import Link from 'next/link';
import { notFound } from 'next/navigation';

import { dashboard, signed } from '@/lib/dashboard';

type PageProps = {
  params: Promise<{ matchId: string; botId: string }>;
};

type Player = (typeof dashboard.matches)[number]['players'][number];

function percent(value: number | null) {
  return value === null ? '—' : `${value.toFixed(1)}%`;
}

/* Bets and raises as a share of every decision that was not a check, the
 * standard aggression frequency. Action types are fold 1, check 2, call 3,
 * raise 4. */
function aggressionFrequency(bot: Player) {
  let aggressive = 0;
  let total = 0;
  for (const action of bot.actions) {
    if (action.action_type === 4) {
      aggressive += action.count;
      total += action.count;
    } else if (action.action_type === 1 || action.action_type === 3) {
      total += action.count;
    }
  }
  return total === 0 ? null : (aggressive / total) * 100;
}

export default async function MatchupPage({ params }: PageProps) {
  const route = await params;
  const match = dashboard.matches.find((item) => item.id === Number(route.matchId));
  if (!match) notFound();

  const player =
    match.players.find((item) => item.bot_id === Number(route.botId)) ?? match.players[0];
  const opponent = match.players.find((item) => item.bot_id !== player.bot_id);
  if (!opponent) notFound();

  const bb = (chips: number) => `${signed(chips / match.big_blind, 1)} BB`;
  const tone = (chips: number) => (chips >= 0 ? 'text-[#087343]' : 'text-[#b52d24]');

  const wtsd = player.wtsd_percentage;
  const headline: Array<[string, number, string]> = [
    ['Raw result', player.raw_net_chips, `${match.hand_count.toLocaleString()} hands`],
    [
      'Preflop result',
      player.preflop_raw_net_chips,
      `${player.preflop_hands.toLocaleString()} hands ended preflop`,
    ],
    [
      'Showdown result',
      player.showdown_raw_net_chips,
      `${percent(wtsd)} of hands`,
    ],
    [
      'Non-showdown result',
      player.nonshowdown_raw_net_chips,
      `${percent(wtsd === null ? null : 100 - wtsd)} of hands`,
    ],
  ];

  const secondary: Array<[string, string]> = [
    ['VPIP / PFR', `${percent(player.vpip_percentage)} / ${percent(player.pfr_percentage)}`],
    ['Aggression frequency', percent(aggressionFrequency(player))],
    ['C-bet %', percent(player.cbet_percentage)],
    ['Showdown % (WTSD)', percent(wtsd)],
    ['All-in reached', percent(player.all_in_reached_percentage)],
    ['Won at showdown', percent(player.w_sd_percentage)],
  ];

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

        <section className="grid gap-3 sm:grid-cols-2 lg:grid-cols-4">
          {headline.map(([label, chips, note]) => (
            <div key={label} className="min-h-28 border border-[#cfc4b6] bg-[#fffdf8] p-5">
              <span className="block text-xs uppercase tracking-[.08em] text-[#756a60]">
                {label}
              </span>
              <strong className={`mt-3 block font-mono text-2xl ${tone(chips)}`}>
                {bb(chips)}
              </strong>
              <span className="mt-2 block text-xs text-[#8b8177]">{note}</span>
            </div>
          ))}
        </section>

        <section className="mt-3 grid grid-cols-2 gap-2 sm:grid-cols-3 lg:grid-cols-6">
          {secondary.map(([label, value]) => (
            <div key={label} className="border border-[#ded5c9] bg-[#fbf8f1] px-3 py-3">
              <span className="block text-[10px] uppercase tracking-[.07em] text-[#8b8177]">
                {label}
              </span>
              <strong className="mt-1.5 block font-mono text-base font-normal text-[#4a423b]">
                {value}
              </strong>
            </div>
          ))}
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
