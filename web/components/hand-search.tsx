import { Link, useNavigate } from 'react-router';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';

import {
  DEFAULT_HAND_SORT,
  HAND_FILTERS,
  HAND_SORTS,
  type HandFilter,
  type HandMeta,
  type HandSummary,
  type HandSummaryTotals,
  consistentFilters,
  potClassLabel,
  filterSiblings,
  fetchHandMeta,
  fetchHands,
  handApi,
  SUIT_GLYPHS,
  suitColor,
} from '@/lib/hands';

const PAGE = 20;
const SAVED_KEY = 'felt.hands.search';

type Saved = {
  bot?: number;
  opponent?: number;
  hand: string;
  filters: HandFilter[];
  sort: string;
  offset: number;
  from?: string;
};

function Card({ card }: { card: string }) {
  const glyph = SUIT_GLYPHS[card.slice(-1)] ?? '';
  return (
    <span
      className="inline-block min-w-[2.1rem] border border-[#cfc4b6] bg-white px-1 py-0.5 text-center font-mono text-xs"
      style={{ color: suitColor(card) }}
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
  initialFrom,
}: {
  initialBot?: number;
  initialOpponent?: number;
  initialHand?: string;
  initialFilters?: HandFilter[];
  initialSort?: string;
  initialOffset?: number;
  initialFrom?: string;
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
  const [sort, setSort] = useState(initialSort ?? DEFAULT_HAND_SORT);
  const [offset, setOffset] = useState(initialOffset ?? 0);
  /* Where this page was opened from, so Back goes there rather than always to
   * the matrix. Carried in the query string by the links that open it and in
   * session storage for the return trip out of a replay. */
  const [from, setFrom] = useState(initialFrom);
  const [totals, setTotals] = useState<HandSummaryTotals | null>(null);

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
          setTotals(page.summary);
        })
        .catch((cause: Error) => {
          setQueryError(cause.message);
          setRows([]);
          setTotal(0);
          setTotals(null);
        })
        .finally(() => setLoading(false));
    },
    [meta, botId, opponentId, committedHand, filters, sort],
  );

  const firstRun = useRef(true);
  const resumeOffset = useRef(initialOffset ?? 0);

  /*
   * The query string carries the search now that the address goes through the
   * router, so this is the second line rather than the only one: it restores
   * the last search when the page is opened with no parameters at all, which
   * is what happens when /hands is reached from a plain link or a new tab
   * rather than from a Back out of a replay. A page opened with parameters of
   * its own ignores it -- an explicit link always wins.
   */
  useEffect(() => {
    if (
      initialBot ||
      initialHand ||
      initialFilters?.length ||
      initialSort ||
      initialFrom
    ) {
      return;
    }
    let saved: Partial<Saved>;
    try {
      const raw = sessionStorage.getItem(SAVED_KEY);
      if (!raw) return;
      saved = JSON.parse(raw) as Partial<Saved>;
    } catch {
      return; /* private mode, or something else wrote there */
    }
    const known = new Set(HAND_FILTERS.map(([filter]) => filter as string));
    if (saved.bot) setBotId(saved.bot);
    if (saved.opponent) setOpponentId(saved.opponent);
    if (saved.hand) {
      setStartingHand(saved.hand);
      setCommittedHand(saved.hand);
    }
    if (Array.isArray(saved.filters)) {
      setFilters(
        consistentFilters(
          saved.filters.filter((item) => known.has(item)) as HandFilter[],
        ),
      );
    }
    if (saved.sort) setSort(saved.sort);
    if (saved.from) setFrom(saved.from);
    resumeOffset.current = saved.offset ?? 0;
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    if (!meta) return;
    if (!botId) {
      setRows([]);
      setTotal(0);
      return;
    }
    const start = firstRun.current ? resumeOffset.current : 0;
    firstRun.current = false;
    run(start);
  }, [meta, botId, run]);

  /*
   * Keep the address in step with the form. Clicking a hand pushes the replay
   * onto the history stack, so Back returns to this entry and the page comes
   * up on the search you left rather than a blank one.
   *
   * This has to go through the router. history.replaceState moves the address
   * bar without telling react-router, so the entry the router restores on Back
   * is still the one it was first rendered with -- which is why the filters
   * and the order came back empty. It also wrote a path with no base path in
   * it, which would have broken the address outright once the site moved under
   * /Felt. navigate() with replace does both jobs properly.
   */
  const navigate = useNavigate();
  useEffect(() => {
    const search = new URLSearchParams();
    if (botId) search.set('bot', String(botId));
    if (opponentId) search.set('opponent', String(opponentId));
    if (committedHand.trim()) search.set('hand', committedHand.trim());
    if (filters.length) search.set('filters', filters.join(','));
    if (sort !== DEFAULT_HAND_SORT) search.set('sort', sort);
    if (offset) search.set('offset', String(offset));
    if (from) search.set('from', from);
    const query = search.toString();
    navigate(query ? `/hands?${query}` : '/hands', { replace: true });
    try {
      sessionStorage.setItem(
        SAVED_KEY,
        JSON.stringify({
          bot: botId,
          opponent: opponentId,
          hand: committedHand.trim(),
          filters,
          sort,
          offset,
          from,
        } satisfies Saved),
      );
    } catch {
      /* storage is a convenience here, never a requirement */
    }
  }, [botId, opponentId, committedHand, filters, sort, offset, from, navigate]);

  /* Every chip stays on screen; picking one clears the alternatives it rules
   * out, so the set is always one that can actually match a hand. Hiding them
   * made the row jump about and hid what else was on offer. */
  const toggle = (filter: HandFilter) =>
    setFilters((current) => {
      if (current.includes(filter)) {
        return current.filter((item) => item !== filter);
      }
      const siblings = filterSiblings(filter);
      return [...current.filter((item) => !siblings.includes(item)), filter];
    });

  const bigBlind = meta?.big_blind ?? 100;
  const selectClass =
    'w-full border border-[#cfc4b6] bg-[#fffdf8] px-3 py-2 font-mono text-sm outline-none focus:border-[#8b8177]';
  const labelClass =
    'mb-1 block text-[10px] uppercase tracking-[.08em] text-[#8b8177]';

  /* The label is worth resolving rather than saying "back": returning to a
   * named bot or matchup tells you where you are without a second look. */
  const back = (() => {
    const bot = from?.match(/^\/bot\/(\d+)$/);
    if (bot && meta) {
      const found = meta.bots.find((item) => item.id === Number(bot[1]));
      if (found) return { href: from!, label: `← ${found.name}` };
    }
    const matchup = from?.match(/^\/matchup\/(\d+)\/\d+$/);
    if (matchup && meta) {
      const found = meta.matchups.find(
        (item) => item.match_id === Number(matchup[1]),
      );
      if (found) {
        return {
          href: from!,
          label: `← ${found.bot_name} vs ${found.opponent_name}`,
        };
      }
    }
    return { href: '/', label: '← Matchup matrix' };
  })();

  const header = (
    <>
      <header className="flex items-center justify-between border-b border-[#bdb2a6] pb-6">
        <Link to={back.href} className="font-semibold hover:underline">
          {back.label}
        </Link>
        <span className="font-mono text-xs uppercase tracking-[.08em] text-[#756a60]">
          local only · reads data/felt.sqlite3
        </span>
      </header>
      <h1 className="py-9 font-serif text-4xl">Hand replay</h1>
    </>
  );

  if (error)
    return (
      <>
        {header}
        <Offline message={error} />
      </>
    );

  return (
    <>
      {header}
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

      {botId && totals && totals.hands > 0 && (
        <section className="mt-5 grid gap-2 grid-cols-2 sm:grid-cols-3 lg:grid-cols-5">
          {(
            [
              [
                'Raw result',
                `${totals.raw_net_chips >= 0 ? '+' : ''}${(totals.raw_net_chips / bigBlind).toFixed(0)} BB`,
                totals.raw_net_chips,
              ],
              [
                'BB / hand',
                `${totals.raw_net_chips >= 0 ? '+' : ''}${(totals.raw_net_chips / bigBlind / totals.hands).toFixed(3)}`,
                totals.raw_net_chips,
              ],
              [
                'Hands won',
                `${((100 * totals.wins) / totals.hands).toFixed(1)}%`,
                0,
              ],
              [
                'Went to showdown',
                `${((100 * totals.showdowns) / totals.hands).toFixed(1)}%`,
                0,
              ],
              [
                'Average pot',
                `${(totals.pot_chips / bigBlind / totals.hands).toFixed(1)} BB`,
                0,
              ],
            ] as Array<[string, string, number]>
          ).map(([label, value, tone]) => (
            <div key={label} className="border border-[#ded5c9] bg-[#fbf8f1] px-3 py-3">
              <span className="block text-[10px] uppercase tracking-[.07em] text-[#8b8177]">
                {label}
              </span>
              <strong
                className={`mt-1.5 block font-mono text-base font-normal ${
                  tone > 0
                    ? 'text-[#087343]'
                    : tone < 0
                      ? 'text-[#b52d24]'
                      : 'text-[#4a423b]'
                }`}
              >
                {value}
              </strong>
            </div>
          ))}
        </section>
      )}

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
                      to={`/hands/${row.match_id}/${row.hand_index}`}
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
                    {potClassLabel(row.pot_class)}
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
