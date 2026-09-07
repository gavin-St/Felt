import Link from 'next/link';
import { notFound } from 'next/navigation';

import { BotGlyph } from '@/components/bot-glyph';
import { HandTable } from '@/components/hand-table';
import { StatBlockView } from '@/components/stat-block';
import { StepLink } from '@/components/step-link';
import { BOT_ORDER, BOT_PROFILES } from '@/lib/bots';
import {
  aggregateBuckets,
  dashboard,
  matchStats,
  signed,
} from '@/lib/dashboard';
import { HAND_REPLAY_ENABLED } from '@/lib/hands';

type PageProps = {
  params: Promise<{ matchId: string; botId: string }>;
};

export default async function MatchupPage({ params }: PageProps) {
  const route = await params;
  const match = dashboard.matches.find(
    (item) => item.id === Number(route.matchId),
  );
  if (!match) notFound();

  const player =
    match.players.find((item) => item.bot_id === Number(route.botId)) ??
    match.players[0];
  const opponent = match.players.find((item) => item.bot_id !== player.bot_id);
  if (!opponent) notFound();
  const playerProfile = BOT_PROFILES[player.bot_name];
  const opponentProfile = BOT_PROFILES[opponent.bot_name];

  const tone = (chips: number) =>
    chips >= 0 ? 'text-[#087343]' : 'text-[#b52d24]';
  const stats = matchStats(player);

  /* Paging keeps the hero fixed and walks its opponents in roster order, the
   * same tier order the bot pages step through, so the arrows mean the same
   * thing on both screens. */
  const siblings = dashboard.matrix
    .filter((result) => result.bot_id === player.bot_id)
    .sort(
      (left, right) =>
        BOT_ORDER.indexOf(left.opponent_name) -
        BOT_ORDER.indexOf(right.opponent_name),
    );
  const here = siblings.findIndex((result) => result.match_id === match.id);
  const sibling = (offset: number) =>
    here === -1 || siblings.length < 2
      ? null
      : siblings[(here + offset + siblings.length) % siblings.length];
  const previousMatch = sibling(-1);
  const nextMatch = sibling(1);
  const matchupHref = (result: NonNullable<typeof previousMatch>) =>
    `/matchup/${result.match_id}/${player.bot_id}`;

  const buckets = aggregateBuckets([{ player, bigBlind: match.big_blind }]);
  const replayBase = `/hands?bot=${player.bot_id}&opponent=${opponent.bot_id}`;

  return (
    <main className="min-h-screen bg-[#f6f2e9] text-[#241f1b]">
      <div className="mx-auto max-w-[1180px] px-6 py-8 pb-20">
        <header className="flex items-center justify-between border-b border-[#bdb2a6] pb-6">
          <Link href="/" className="font-semibold hover:underline">
            ← Matchup matrix
          </Link>
          <span className="flex items-center gap-4 font-mono text-xs uppercase tracking-[.08em]">
            {HAND_REPLAY_ENABLED && (
              <Link
                href={replayBase}
                className="border border-[#cfc4b6] bg-[#fffdf8] px-3 py-1.5 hover:bg-[#fff]"
              >
                Replay these hands →
              </Link>
            )}
            <span>
              {match.hand_count.toLocaleString()} hands · seed {match.match_seed}
            </span>
          </span>
        </header>

        <section className="group relative grid items-center gap-6 py-11 md:grid-cols-[1fr_auto_1fr]">
          {previousMatch ? (
            <StepLink
              href={matchupHref(previousMatch)}
              direction="previous"
              label={`Previous matchup: versus ${previousMatch.opponent_name}`}
              offset="top-1/2"
            />
          ) : null}
          <div>
            <p className="font-mono text-xs uppercase tracking-[.08em]">
              Hero bot
            </p>
            <div className="mt-1 flex items-center gap-3">
              {playerProfile ? (
                <Link
                  href={`/bot/${player.bot_id}`}
                  aria-label={`Open ${player.bot_name}`}
                  className="flex h-10 w-10 shrink-0 items-center justify-center border"
                  style={{
                    borderColor: playerProfile.color,
                    background: `${playerProfile.color}14`,
                  }}
                >
                  <BotGlyph
                    glyph={playerProfile.glyph}
                    color={playerProfile.color}
                    size={25}
                  />
                </Link>
              ) : null}
              <h1 className="min-w-0 truncate font-serif text-4xl">
                <Link
                  href={`/bot/${player.bot_id}`}
                  className="hover:underline"
                >
                  {player.bot_name}
                </Link>
              </h1>
            </div>
            <p className="mt-2 text-sm">
              {player.wins.toLocaleString()} wins ·{' '}
              {player.losses.toLocaleString()} losses
            </p>
          </div>
          <div className="md:text-center">
            <strong
              className={`font-mono text-3xl ${tone(player.adjusted_bb_per_hand)}`}
            >
              {signed(player.adjusted_bb_per_hand)}
            </strong>
            <span className="mt-1 block text-sm text-[#756a60]">
              adjusted bb / hand
            </span>
          </div>
          <div className="md:text-right">
            <p className="font-mono text-xs uppercase tracking-[.08em]">
              Opponent
            </p>
            <div className="mt-1 flex items-center gap-3 md:justify-end">
              <h2 className="font-serif text-4xl">
                <Link
                  href={`/bot/${opponent.bot_id}`}
                  className="hover:underline"
                >
                  {opponent.bot_name}
                </Link>
              </h2>
              {opponentProfile ? (
                <Link
                  href={`/bot/${opponent.bot_id}`}
                  aria-label={`Open ${opponent.bot_name}`}
                  className="flex h-10 w-10 shrink-0 items-center justify-center border"
                  style={{
                    borderColor: opponentProfile.color,
                    background: `${opponentProfile.color}14`,
                  }}
                >
                  <BotGlyph
                    glyph={opponentProfile.glyph}
                    color={opponentProfile.color}
                    size={25}
                  />
                </Link>
              ) : null}
            </div>
            <p className="mt-2 text-sm">
              {opponent.wins.toLocaleString()} wins ·{' '}
              {opponent.losses.toLocaleString()} losses
            </p>
          </div>
          {nextMatch ? (
            <StepLink
              href={matchupHref(nextMatch)}
              direction="next"
              label={`Next matchup: versus ${nextMatch.opponent_name}`}
              offset="top-1/2"
            />
          ) : null}
        </section>

        <section>
          <StatBlockView stats={stats} />
        </section>

        <section className="mt-9">
          <h2 className="mb-4 font-serif text-2xl">
            {HAND_REPLAY_ENABLED ? (
              <Link href={replayBase} className="hover:underline">
                {player.bot_name} starting hands
              </Link>
            ) : (
              `${player.bot_name} starting hands`
            )}
          </h2>
          <HandTable
            rows={buckets}
            replayBase={HAND_REPLAY_ENABLED ? replayBase : undefined}
          />
        </section>
      </div>
    </main>
  );
}
