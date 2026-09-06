import Link from 'next/link';
import { notFound } from 'next/navigation';

import { BotGlyph } from '@/components/bot-glyph';
import { BotPageMode } from '@/components/bot-page-mode';
import { StepLink } from '@/components/step-link';
import {
  BOT_PROFILES,
  RANKS,
  SECRET_BOT,
  SECRET_SLUG,
  botNeighbours,
} from '@/lib/bots';
import { HandTable } from '@/components/hand-table';
import { StatBlockView } from '@/components/stat-block';
import {
  aggregateBuckets,
  botEntries,
  dashboard,
  resultTone,
  signed,
  botStats,
} from '@/lib/dashboard';
import { preflopActionStyles } from '@/lib/preflop';

type PageProps = { params: Promise<{ botId: string }> };

function RangeGrid({ range }: { range: string }) {
  return (
    <div className="overflow-x-auto">
      <table className="border-collapse font-mono text-[9px] leading-none">
        <tbody>
          {Array.from({ length: 13 }, (_, row) => (
            <tr key={row}>
              {Array.from({ length: 13 }, (_, column) => {
                const on = range[row * 13 + column] === '1';
                const high = RANKS[Math.min(row, column)];
                const low = RANKS[Math.max(row, column)];
                const label =
                  row === column
                    ? `${high}${high}`
                    : `${high}${low}${row < column ? 'o' : 's'}`;
                return (
                  <td
                    key={column}
                    title={label}
                    className="h-[22px] w-[22px] border border-[#e6ded3] text-center align-middle"
                    style={
                      on
                        ? preflopActionStyles['all-in']
                        : preflopActionStyles.fold
                    }
                  >
                    {label}
                  </td>
                );
              })}
            </tr>
          ))}
        </tbody>
      </table>
      <p className="mt-2 font-serif text-xs italic text-[#756b60]">
        Above the diagonal is suited, below is offsuit, the diagonal is pairs.
      </p>
    </div>
  );
}

export default async function BotPage({ params }: PageProps) {
  const route = await params;
  /* Rated bots are addressed by their ledger id, but a bot that has not played
   * a match yet does not have one, so its slug works as the route too. Those
   * pages carry the writing and nothing else: there is no Elo, no stat block,
   * no starting hands and no record until the matches exist. */
  const key = decodeURIComponent(route.botId);
  const rating =
    dashboard.ratings.find((bot) => bot.bot_id === Number(key)) ??
    dashboard.ratings.find((bot) => bot.name === key);
  const profile =
    key === SECRET_SLUG
      ? SECRET_BOT
      : BOT_PROFILES[rating ? rating.name : key];
  if (!profile) notFound();
  const name = rating?.name ?? profile.slug;

  const ranked = [...dashboard.ratings].sort(
    (left, right) => right.elo - left.elo,
  );
  const rank = rating
    ? ranked.findIndex((bot) => bot.bot_id === rating.bot_id) + 1
    : 0;

  const entries = rating ? botEntries(rating.bot_id) : [];
  const stats = botStats(rating ? rating.bot_id : -1);
  const buckets = aggregateBuckets(entries);

  const record = rating
    ? dashboard.matrix
        .filter((result) => result.bot_id === rating.bot_id)
        .sort(
          (left, right) =>
            right.adjusted_bb_per_hand - left.adjusted_bb_per_hand,
        )
    : [];

  /* Rated bots keep their ledger id as the canonical URL, the way the matrix
   * links to them; a bot with no matches yet has only its slug. */
  const href = (slug: string) => {
    const rated = dashboard.ratings.find((bot) => bot.name === slug);
    return `/bot/${rated ? rated.bot_id : slug}`;
  };
  const neighbours = botNeighbours(profile.slug);

  return (
    <main className="min-h-screen bg-[#faf6ee] px-6 py-10 text-[#231f1b]">
      <div className="mx-auto max-w-4xl">
        <Link href="/" className="font-mono text-xs text-[#756b60] underline">
          ← All bots
        </Link>

        <BotPageMode>
          <header className="group relative mt-6 flex items-start gap-5 border-b-2 border-[#231f1b] pb-6">
            {neighbours.previous ? (
              <StepLink
                href={href(neighbours.previous)}
                direction="previous"
                label={`Previous bot: ${neighbours.previous}`}
                offset="top-[82%]"
              />
            ) : null}
            <div
              className="flex h-20 w-20 shrink-0 items-center justify-center border-2"
              style={{
                borderColor: profile.color,
                background: `${profile.color}14`,
              }}
            >
              <BotGlyph glyph={profile.glyph} color={profile.color} size={44} />
            </div>
            <div className="min-w-0">
              <h1 className="truncate font-mono text-3xl font-semibold tracking-tight">
                {name}
              </h1>
              <p className="mt-3 font-serif text-lg italic">
                {profile.tagline}
              </p>
            </div>
            {rating ? (
              <div className="bot-analytics ml-auto shrink-0 text-right">
                <p className="font-mono text-3xl font-semibold">
                  {rating.elo.toFixed(0)}
                </p>
                <p className="font-mono text-[11px] text-[#756b60]">
                  Elo ±{(1.96 * rating.standard_error).toFixed(0)}
                </p>
                <p className="mt-1 font-mono text-[11px] font-semibold text-[#b42c23]">
                  #{rank} of {ranked.length}
                </p>
              </div>
            ) : (
              <div className="bot-analytics ml-auto shrink-0 text-right">
                <p className="font-mono text-[11px] uppercase tracking-[.08em] text-[#8b8177]">
                  Unrated
                </p>
                <p className="mt-1 max-w-[9rem] text-[11px] leading-snug text-[#8b8177]">
                  Built, tested, and waiting for its first match.
                </p>
              </div>
            )}
            {neighbours.next ? (
              <StepLink
                href={href(neighbours.next)}
                direction="next"
                label={`Next bot: ${neighbours.next}`}
                offset="top-[82%]"
              />
            ) : null}
          </header>

          {stats.matches > 0 ? (
            <section className="bot-analytics mt-6">
              <h2 className="mb-4 border-b border-[#d8cfc2] pb-2 text-sm font-semibold uppercase tracking-wide">
                Across {stats.matches} recorded matchup
                {stats.matches === 1 ? '' : 's'}
                <span className="ml-2 font-mono text-[11px] font-normal normal-case tracking-normal text-[#8b8177]">
                  {stats.hands.toLocaleString()} hands
                </span>
              </h2>
              <StatBlockView stats={stats} />
            </section>
          ) : null}

          <section className="mt-8">
            <h2 className="border-b border-[#d8cfc2] pb-2 text-sm font-semibold uppercase tracking-wide">
              How it plays
            </h2>
            <div className="mt-4 grid gap-3 sm:grid-cols-3">
              {profile.stats.map((stat) => (
                <div
                  key={stat.label}
                  className="border border-[#ded5c9] bg-[#fbf8f1] px-3 py-3"
                >
                  <p className="text-[10px] uppercase tracking-[.07em] text-[#8b8177]">
                    {stat.label}
                  </p>
                  <p className="mt-1.5 font-mono text-sm text-[#4a423b]">
                    {stat.value}
                  </p>
                </div>
              ))}
            </div>
            {profile.story.map((paragraph) => (
              <p
                key={paragraph.slice(0, 40)}
                className="mt-4 max-w-2xl leading-relaxed"
              >
                {paragraph}
              </p>
            ))}
          </section>

          {profile.range ? (
            <section className="bot-analytics mt-8">
              <h2 className="border-b border-[#d8cfc2] pb-2 text-sm font-semibold uppercase tracking-wide">
                Range
              </h2>
              <p className="mt-3 font-mono text-xs text-[#756b60]">
                {profile.rangeLabel}
              </p>
              <div className="mt-4">
                <RangeGrid range={profile.range} />
              </div>
            </section>
          ) : null}

          {buckets.length > 0 ? (
            <section className="bot-analytics mt-8">
              <h2 className="border-b border-[#d8cfc2] pb-2 text-sm font-semibold uppercase tracking-wide">
                Starting hands
                <span className="ml-2 font-mono text-[11px] font-normal normal-case tracking-normal text-[#8b8177]">
                  every recorded matchup, equity-adjusted
                </span>
              </h2>
              <div className="mt-4">
                <HandTable rows={buckets} />
              </div>
            </section>
          ) : null}

          {record.length > 0 ? (
            <section className="bot-analytics mt-8">
            <h2 className="border-b border-[#d8cfc2] pb-2 text-sm font-semibold uppercase tracking-wide">
              Record
            </h2>
            <table className="mt-4 w-full border-collapse border border-[#cfc4b6] bg-[#fffdf8] text-sm">
                <thead>
                  <tr>
                    <th className="border-b border-[#e3dbd0] p-3 text-left">
                      Opponent
                    </th>
                    <th className="border-b border-[#e3dbd0] p-3 text-right">
                      BB / hand
                    </th>
                    <th className="border-b border-[#e3dbd0] p-3 text-right">
                      Hands
                    </th>
                    <th className="border-b border-[#e3dbd0] p-3 text-right">
                      Report
                    </th>
                  </tr>
                </thead>
                <tbody>
                  {record.map((result) => {
                    const opponent = BOT_PROFILES[result.opponent_name];
                    return (
                      <tr key={result.match_id}>
                        <td className="border-b border-[#e3dbd0] p-3">
                          <Link
                            href={`/bot/${result.opponent_bot_id}`}
                            className="inline-flex items-center gap-2 underline"
                          >
                            {opponent ? (
                              <BotGlyph
                                glyph={opponent.glyph}
                                color={opponent.color}
                                size={14}
                              />
                            ) : null}
                            {result.opponent_name}
                          </Link>
                        </td>
                        <td
                          className="border-b border-[#e3dbd0] p-3 text-right font-mono"
                          style={resultTone(result.adjusted_bb_per_hand)}
                        >
                          {signed(result.adjusted_bb_per_hand)}
                        </td>
                        <td className="border-b border-[#e3dbd0] p-3 text-right font-mono text-[#756b60]">
                          {result.hand_count.toLocaleString()}
                        </td>
                        <td className="border-b border-[#e3dbd0] p-3 text-right">
                          <Link
                            href={`/matchup/${result.match_id}/${result.bot_id}`}
                            className="font-mono text-xs underline"
                          >
                            open
                          </Link>
                        </td>
                      </tr>
                    );
                  })}
                </tbody>
              </table>
            </section>
          ) : (
            <section className="mt-8 border border-dashed border-[#cfc4b6] bg-[#fbf8f1] p-6">
              <h2 className="text-sm font-semibold uppercase tracking-wide">
                No matches yet
              </h2>
              <p className="mt-3 max-w-2xl leading-relaxed">
                {profile.unratedNote ??
                  `${name} is built and its behaviour is pinned by tests, but it has not been entered into the ledger. Elo, the stat block, the starting-hand table and the record all appear here on its first recorded match.`}
              </p>
            </section>
          )}
        </BotPageMode>
      </div>
    </main>
  );
}
