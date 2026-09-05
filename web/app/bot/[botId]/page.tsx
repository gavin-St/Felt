import Link from 'next/link';
import { notFound } from 'next/navigation';

import { BotGlyph } from '@/components/bot-glyph';
import { BOT_PROFILES, RANKS } from '@/lib/bots';
import { dashboard, resultTone, signed } from '@/lib/dashboard';

type PageProps = { params: Promise<{ botId: string }> };

function RangeGrid({ range, color }: { range: string; color: string }) {
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
                  row === column ? `${high}${high}` : `${high}${low}${row < column ? 'o' : 's'}`;
                return (
                  <td
                    key={column}
                    title={label}
                    className="h-[22px] w-[22px] border border-[#e6ded3] text-center align-middle"
                    style={
                      on
                        ? { background: color, color: '#fffdf8' }
                        : { background: '#fffdf8', color: '#b3a998' }
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
  const rating = dashboard.ratings.find((bot) => bot.bot_id === Number(route.botId));
  if (!rating) notFound();
  const profile = BOT_PROFILES[rating.name];
  if (!profile) notFound();

  const ranked = [...dashboard.ratings].sort((left, right) => right.elo - left.elo);
  const rank = ranked.findIndex((bot) => bot.bot_id === rating.bot_id) + 1;

  const record = dashboard.matrix
    .filter((result) => result.bot_id === rating.bot_id)
    .sort((left, right) => right.adjusted_bb_per_hand - left.adjusted_bb_per_hand);

  return (
    <main className="min-h-screen bg-[#faf6ee] px-6 py-10 text-[#231f1b]">
      <div className="mx-auto max-w-4xl">
        <Link href="/" className="font-mono text-xs text-[#756b60] underline">
          ← All bots
        </Link>

        <header className="mt-6 flex items-start gap-5 border-b-2 border-[#231f1b] pb-6">
          <div
            className="flex h-20 w-20 shrink-0 items-center justify-center border-2"
            style={{ borderColor: profile.color, background: `${profile.color}14` }}
          >
            <BotGlyph glyph={profile.glyph} color={profile.color} size={44} />
          </div>
          <div className="min-w-0">
            <h1 className="text-3xl font-semibold tracking-tight">{profile.character}</h1>
            <p className="mt-1 font-mono text-sm text-[#756b60]">{rating.name}</p>
            <p className="mt-3 font-serif text-lg italic">{profile.tagline}</p>
          </div>
          <div className="ml-auto shrink-0 text-right">
            <p className="font-mono text-3xl font-semibold">{rating.elo.toFixed(0)}</p>
            <p className="font-mono text-[11px] text-[#756b60]">
              Elo ±{(1.96 * rating.standard_error).toFixed(0)}
            </p>
            <p className="mt-1 font-mono text-[11px] font-semibold text-[#b42c23]">
              #{rank} of {ranked.length}
            </p>
          </div>
        </header>

        <section className="mt-6 grid grid-cols-3 gap-3">
          {profile.stats.map((stat) => (
            <div key={stat.label} className="border border-[#cfc4b6] bg-[#fffdf8] p-4">
              <p className="font-mono text-[11px] uppercase tracking-wide text-[#756b60]">
                {stat.label}
              </p>
              <p className="mt-1 font-mono text-xl">{stat.value}</p>
            </div>
          ))}
        </section>

        <section className="mt-8">
          <h2 className="border-b border-[#d8cfc2] pb-2 text-sm font-semibold uppercase tracking-wide">
            How it plays
          </h2>
          {profile.story.map((paragraph) => (
            <p key={paragraph.slice(0, 40)} className="mt-4 max-w-2xl leading-relaxed">
              {paragraph}
            </p>
          ))}
        </section>

        {profile.range ? (
          <section className="mt-8">
            <h2 className="border-b border-[#d8cfc2] pb-2 text-sm font-semibold uppercase tracking-wide">
              Range
            </h2>
            <p className="mt-3 font-mono text-xs text-[#756b60]">{profile.rangeLabel}</p>
            <div className="mt-4">
              <RangeGrid range={profile.range} color={profile.color} />
            </div>
          </section>
        ) : null}

        <section className="mt-8">
          <h2 className="border-b border-[#d8cfc2] pb-2 text-sm font-semibold uppercase tracking-wide">
            Record
          </h2>
          <table className="mt-4 w-full border-collapse border border-[#cfc4b6] bg-[#fffdf8] text-sm">
            <thead>
              <tr>
                <th className="border-b border-[#e3dbd0] p-3 text-left">Opponent</th>
                <th className="border-b border-[#e3dbd0] p-3 text-right">BB / hand</th>
                <th className="border-b border-[#e3dbd0] p-3 text-right">Hands</th>
                <th className="border-b border-[#e3dbd0] p-3 text-right">Report</th>
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
                          <BotGlyph glyph={opponent.glyph} color={opponent.color} size={14} />
                        ) : null}
                        {opponent?.character ?? result.opponent_name}
                      </Link>
                      <span className="ml-2 font-mono text-[11px] text-[#756b60]">
                        {result.opponent_name}
                      </span>
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
                        href={`/matchup/${result.match_id}/${rating.bot_id}`}
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
      </div>
    </main>
  );
}
