'use client';

import Link from 'next/link';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';

import {
  HAND_FILTERS,
  HAND_SORTS,
  type HandFilter,
  type HandMeta,
  type HandSummary,
  fetchHandMeta,
  fetchHands,
  handApi,
} from '@/lib/hands';

const PAGE = 20;

const POT_CLASS_LABELS: Record<string, string> = {
  walk: 'Walk',
  limped_unraised: 'Limped',
  single_raised: 'Single raised',
  three_bet: 'Three-bet',
  four_bet_plus: 'Four-bet+',
};

function Card({ card }: { card: string }) {
  const red = card.endsWith('d') || card.endsWith('h');
  const glyph = { c: '♣', d: '♦', s: '♠', h: '♥' }[card.slice(-1)] ?? '';
  return (
    <span
      className={`inline-block min-w-[2.1rem] border border-[#cfc4b6] bg-white px-1 py-0.5 text-center font-mono text-xs ${
        red ? 'text-[#b52d24]' : 'text-[#241f1b]'
      }`}
    >
      {card.slice(0, -1)}
      {glyph}
    </span>
  );
}

function Offline({ message }: { message: string }) {
  return (
    <section className="border border-[#cfc4b6] bg-[#fffdf8] p-8">
      <h2 className="font-serif text-2xl">The hand server is not answering</h2>
      <p className="mt-3 max-w-[62ch] text-sm text-[#5c534b]">
        Replay reads the ledger directly rather than from the bundled snapshot,
        so it needs the machine that has <code>data/felt.sqlite3</code>. Start it
        beside the dev server:
      </p>
      <pre className="mt-4 overflow-x-auto border border-[#e3dbd0] bg-[#f6f2e9] p-4 font-mono text-xs">
        python3 scripts/hand_server.py --database data/felt.sqlite3
      </pre>
      <p className="mt-4 text-xs text-[#8b8177]">
        Tried {handApi()} — {message}
      </p>
    </section>
  );
}

export function HandSearch({
  initialBot,
  initialOpponent,
  initialHand,
  initialFilters,
  initialSort,
  initialOffset,
}: {
  initialBot?: number;
  initialOpponent?: number;
  initialHand?: string;
  initialFilters?: HandFilter[];
  initialSort?: string;
  initialOffset?: number;
}) {
  const [meta, setMeta] = useState<HandMeta | null>(null);
  const [error, setError] = useState<string | null>(null);

  const [botId, setBotId] = useState<number | undefined>(initialBot);
  const [opponentId, setOpponentId] = useState<number | undefined>(initialOpponent);
  const [startingHand, setStartingHand] = useState(initialHand ?? '');
  /* Typing is not a search. `run` closes over the committed value, so the
   * effect below cannot re-fire on every keystroke. */
  const [committedHand, setCommittedHand] = useState(initialHand ?? '');
  const [filters, setFilters] = useState<HandFilter[]>(initialFilters ?? []);
  const [sort, setSort] = useState(initialSort ?? 'random');
  const [offset, setOffset] = useState(initialOffset ?? 0);

  const [rows, setRows] = useState<HandSummary[]>([]);
  const [total, setTotal] = useState(0);
  const [loading, setLoading] = useState(false);
  const [queryError, setQueryError] = useState<string | null>(null);

  useEffect(() => {
    fetchHandMeta()
      .then(setMeta)
      .catch((cause: Error) => setError(cause.message));
  }, []);

  /* Opponents are the bots this one actually has a match against, so the two
   * pickers can never combine into a matchup that was never played. */
  const opponents = useMemo(() => {
    if (!meta || !botId) return [];
    const names = new Map<number, string>();
    for (const row of meta.matchups) {
      if (row.bot_id === botId) names.set(row.opponent_bot_id, row.opponent_name);
      if (row.opponent_bot_id === botId) names.set(row.bot_id, row.bot_name);
    }
    return [...names].sort((left, right) => left[1].localeCompare(right[1]));
  }, [meta, botId]);

  const run = useCallback(
    (nextOffset: number) => {
      if (!meta) return;
      setLoading(true);
      setQueryError(null);
      fetchHands({
        bot: botId,
        opponent: opponentId,
        hand: committedHand.trim() || undefined,
        filters,
        sort,
        limit: PAGE,
        offset: nextOffset,
      })
        .then((page) => {
          setRows(page.hands);
          setTotal(page.total);
          setOffset(page.offset);
        })
        .catch((cause: Error) => {
          setQueryError(cause.message);
          setRows([]);
          setTotal(0);
        })
        .finally(() => setLoading(false));
    },
    [meta, botId, opponentId, committedHand, filters, sort],
  );

  const firstRun = useRef(true);
  useEffect(() => {
    if (!meta) return;
    if (!botId) {
      setRows([]);
      setTotal(0);
      return;
    }
    const start = firstRun.current ? (initialOffset ?? 0) : 0;
    firstRun.current = false;
    run(start);
  }, [meta, botId, run, initialOffset]);

  /* Keep the query string in step with the form, without navigating. Clicking
   * a hand pushes the replay onto the history stack, so Back returns to this
   * URL and the page comes up on the same search rather than a blank one. */
  useEffect(() => {
    const search = new URLSearchParams();
    if (botId) search.set('bot', String(botId));
    if (opponentId) search.set('opponent', String(opponentId));
    if (committedHand.trim()) search.set('hand', committedHand.trim());
    if (filters.length) search.set('filters', filters.join(','));
    if (sort !== 'random') search.set('sort', sort);
    if (offset) search.set('offset', String(offset));
    const query = search.toString();
    window.history.replaceState(
      window.history.state,
      '',
      query ? `/hands?${query}` : '/hands',
    );
  }, [botId, opponentId, committedHand, filters, sort, offset]);

  const toggle = (filter: HandFilter) =>
    setFilters((current) =>
      current.includes(filter)
        ? current.filter((item) => item !== filter)
        : [...current, filter],
    );

  const bigBlind = meta?.big_blind ?? 100;
  const selectClass =
    'w-full border border-[#cfc4b6] bg-[#fffdf8] px-3 py-2 font-mono text-sm outline-none focus:border-[#8b8177]';
  const labelClass =
    'mb-1 block text-[10px] uppercase tracking-[.08em] text-[#8b8177]';

  if (error) return <Offline message={error} />;

  return (
    <>
      <section className="border border-[#cfc4b6] bg-[#fffdf8] p-5">
        <div className="grid gap-4 md:grid-cols-4">
          <div>
            <label className={labelClass} htmlFor="hand-bot">
              Hero bot
            </label>
            <select
              id="hand-bot"
              className={selectClass}
              value={botId ?? ''}
              onChange={(event) => {
                setBotId(event.target.value ? Number(event.target.value) : undefined);
                setOpponentId(undefined);
              }}
            >
              <option value="">Choose a bot…</option>
              {meta?.bots.map((bot) => (
                <option key={bot.id} value={bot.id}>
                  {bot.name}
                </option>
              ))}
            </select>
          </div>
          <div>
            <label className={labelClass} htmlFor="hand-opponent">
              Opponent
            </label>
            <select
              id="hand-opponent"
              className={selectClass}
              value={opponentId ?? ''}
              disabled={!botId}
              onChange={(event) =>
                setOpponentId(event.target.value ? Number(event.target.value) : undefined)
              }
            >
              <option value="">{botId ? 'Every opponent' : 'Pick a hero bot first'}</option>
              {opponents.map(([id, name]) => (
                <option key={id} value={id}>
                  {name}
                </option>
              ))}
            </select>
          </div>
          <div>
            <label className={labelClass} htmlFor="hand-bucket">
              Starting hand
            </label>
            <form
              onSubmit={(event) => {
                event.preventDefault();
                setCommittedHand(startingHand);
              }}
            >
              <input
                id="hand-bucket"
                className={selectClass}
                placeholder="KK, AKs, T9o"
                value={startingHand}
                onChange={(event) => setStartingHand(event.target.value)}
                onBlur={() => setCommittedHand(startingHand)}
              />
            </form>
          </div>
          <div>
            <label className={labelClass} htmlFor="hand-sort">
              Order
            </label>
            <select
              id="hand-sort"
              className={selectClass}
              value={sort}
              onChange={(event) => setSort(event.target.value)}
            >
              {HAND_SORTS.map(([value, label]) => (
                <option key={value} value={value}>
                  {label}
                </option>
              ))}
            </select>
          </div>
        </div>

        <div className="mt-4 flex flex-wrap gap-2">
          {HAND_FILTERS.map(([filter, label, hint]) => {
            const on = filters.includes(filter);
            return (
              <button
                key={filter}
                type="button"
                title={hint}
                onClick={() => toggle(filter)}
                className={`border px-3 py-1.5 text-xs transition-colors ${
                  on
                    ? 'border-[#29231d] bg-[#29231d] text-[#f6f2e9]'
                    : 'border-[#cfc4b6] bg-[#fbf8f1] text-[#4a423b] hover:border-[#8b8177]'
                }`}
              >
                {label}
              </button>
            );
          })}
          {filters.length > 0 && (
            <button
              type="button"
              onClick={() => setFilters([])}
              className="px-3 py-1.5 text-xs text-[#8b8177] underline hover:text-[#241f1b]"
            >
              clear
            </button>
          )}
        </div>
      </section>

      {!botId ? (
        <section className="mt-5 border border-[#cfc4b6] bg-[#fffdf8] p-12 text-center">
          <p className="font-serif text-2xl">Pick a bot</p>
          <p className="mx-auto mt-2 max-w-[48ch] text-sm text-[#5c534b]">
            There are 4.62 million hands in the ledger, and no useful way to
            show all of them at once. Choose a hero bot above, then narrow it
            down.
          </p>
        </section>
      ) : (
      <section className="mt-5">
        <div className="mb-3 flex items-baseline justify-between">
          <p className="font-mono text-xs uppercase tracking-[.08em] text-[#756a60]">
            {loading
              ? 'searching…'
              : total === 0
                ? 'no hands match'
                : `${(offset + 1).toLocaleString()}–${Math.min(offset + PAGE, total).toLocaleString()} of ${total.toLocaleString()}`}
          </p>
          <div className="flex gap-2">
            <button
              type="button"
              disabled={offset === 0 || loading}
              onClick={() => run(Math.max(0, offset - PAGE))}
              className="border border-[#cfc4b6] bg-[#fffdf8] px-3 py-1 text-xs disabled:opacity-35"
            >
              ← Previous
            </button>
            <button
              type="button"
              disabled={offset + PAGE >= total || loading}
              onClick={() => run(offset + PAGE)}
              className="border border-[#cfc4b6] bg-[#fffdf8] px-3 py-1 text-xs disabled:opacity-35"
            >
              Next →
            </button>
          </div>
        </div>

        {queryError && (
          <p className="border border-[#e0b4ae] bg-[#fdf3f1] p-3 text-sm text-[#b52d24]">
            {queryError}
          </p>
        )}

        <div className="overflow-x-auto border border-[#cfc4b6] bg-[#fffdf8]">
          <table className="w-full border-collapse text-sm">
            <thead>
              <tr className="text-left text-[10px] uppercase tracking-[.07em] text-[#8b8177]">
                <th className="border-b border-[#e3dbd0] p-3">Matchup</th>
                <th className="border-b border-[#e3dbd0] p-3">Hand</th>
                <th className="border-b border-[#e3dbd0] p-3">Hero</th>
                <th className="border-b border-[#e3dbd0] p-3">Board</th>
                <th className="border-b border-[#e3dbd0] p-3">Pot type</th>
                <th className="border-b border-[#e3dbd0] p-3 text-right">Pot</th>
                <th className="border-b border-[#e3dbd0] p-3 text-right">Result</th>
              </tr>
            </thead>
            <tbody>
              {rows.map((row) => (
                <tr key={`${row.match_id}-${row.hand_index}`} className="hover:bg-[#f6f2e9]">
                  <td className="border-b border-[#e3dbd0] p-3">
                    <Link
                      href={`/hands/${row.match_id}/${row.hand_index}`}
                      className="hover:underline"
                    >
                      <span className="font-medium">{row.bot_name}</span>
                      <span className="text-[#8b8177]"> vs {row.opponent_name}</span>
                    </Link>
                  </td>
                  <td className="border-b border-[#e3dbd0] p-3 font-mono text-xs text-[#756a60]">
                    #{row.hand_index.toLocaleString()}
                    <span className="ml-2">
                      {row.position === 0 ? 'BTN' : 'BB'}
                    </span>
                  </td>
                  <td className="border-b border-[#e3dbd0] p-3">
                    <span className="mr-2 font-mono text-xs text-[#756a60]">
                      {row.bucket}
                    </span>
                    <span className="inline-flex gap-1">
                      <Card card={row.exact_combo.slice(0, 2)} />
                      <Card card={row.exact_combo.slice(2)} />
                    </span>
                  </td>
                  <td className="border-b border-[#e3dbd0] p-3">
                    {row.board.length === 0 ? (
                      <span className="text-xs text-[#a89f93]">—</span>
                    ) : (
                      <span className="inline-flex gap-1">
                        {row.board.map((card, index) => (
                          <Card key={`${card}-${index}`} card={card} />
                        ))}
                      </span>
                    )}
                  </td>
                  <td className="border-b border-[#e3dbd0] p-3 text-xs text-[#756a60]">
                    {POT_CLASS_LABELS[row.pot_class] ?? row.pot_class}
                    {row.all_in_reached === 1 && (
                      <span className="ml-2 border border-[#cfc4b6] px-1 text-[10px] uppercase">
                        all-in
                      </span>
                    )}
                    {row.showdown === 1 && (
                      <span className="ml-2 border border-[#cfc4b6] px-1 text-[10px] uppercase">
                        showdown
                      </span>
                    )}
                  </td>
                  <td className="border-b border-[#e3dbd0] p-3 text-right font-mono text-xs">
                    {(row.final_pot_chips / bigBlind).toFixed(1)}
                  </td>
                  <td
                    className={`border-b border-[#e3dbd0] p-3 text-right font-mono ${
                      row.raw_net_chips > 0
                        ? 'text-[#087343]'
                        : row.raw_net_chips < 0
                          ? 'text-[#b52d24]'
                          : 'text-[#756a60]'
                    }`}
                  >
                    {row.raw_net_chips > 0 ? '+' : ''}
                    {(row.raw_net_chips / bigBlind).toFixed(1)}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </section>
      )}
    </>
  );
}
