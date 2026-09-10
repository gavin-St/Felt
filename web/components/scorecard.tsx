'use client';

import Link from 'next/link';
import { Fragment, useMemo, useState } from 'react';
import { ChevronDown } from 'lucide-react';

import {
  DropdownMenu,
  DropdownMenuCheckboxItem,
  DropdownMenuContent,
  DropdownMenuGroup,
  DropdownMenuLabel,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from '@/components/ui/dropdown-menu';
import { Checkbox } from '@/components/ui/checkbox';
import {
  BOT_GROUPS,
  BOT_ORDER,
  botGroup,
  botGroupIndex,
  groupShade,
  shadeLit,
} from '@/lib/bots';
import {
  dashboard,
  matrixResult,
  resultTone,
  signed,
  subsetRatings,
  type RatingFormula,
} from '@/lib/dashboard';

type SortState = {
  botId: number;
  direction: 'asc' | 'desc';
} | null;

export function Scorecard() {
  const [enabled, setEnabled] = useState(
    () => new Set(dashboard.ratings.map((bot) => bot.bot_id)),
  );
  const [focusedRows, setFocusedRows] = useState(() => new Set<number>());
  /*
   * Hovering a header previews the line it belongs to. It shares the tint a
   * clicked row gets, but not the dimming: pointing at something should not
   * take the rest of the table away, only say which line is which. Clicking
   * still latches, and a latched row stays lit while the pointer is
   * elsewhere.
   */
  const [hoveredRow, setHoveredRow] = useState<number | null>(null);
  const [hoveredColumn, setHoveredColumn] = useState<number | null>(null);
  const [sortState, setSortState] = useState<SortState>(null);
  const [ratingFormula, setRatingFormula] =
    useState<RatingFormula>('outcome-first');
  const bots = useMemo(
    () => subsetRatings(enabled, ratingFormula),
    [enabled, ratingFormula],
  );
  /* The picker lists the field in the same ranking order the matrix rows and
   * the paging arrows use, so a bot is in the same place wherever it is
   * looked for. Its own Elo is the full-field one, not the subset's, because
   * the list includes the bots that are switched off. */
  const roster = useMemo(
    () =>
      [...dashboard.ratings].sort((left, right) => {
        const tier = botGroupIndex(left.name) - botGroupIndex(right.name);
        return tier !== 0
          ? tier
          : BOT_ORDER.indexOf(left.name) - BOT_ORDER.indexOf(right.name);
      }),
    [],
  );
  /* One entry per tier, with the ledger ids it covers. */
  const groups = useMemo(
    () =>
      BOT_GROUPS.map((group) => ({
        ...group,
        botIds: dashboard.ratings
          .filter((bot) => group.slugs.includes(bot.name))
          .map((bot) => bot.bot_id),
      })).filter((group) => group.botIds.length > 0),
    [],
  );
  const setGroup = (botIds: number[], checked: boolean) =>
    setEnabled((current) => {
      const next = new Set(current);
      for (const id of botIds) {
        if (checked) next.add(id);
        else next.delete(id);
      }
      /* Two bots is the floor: a matrix of one has nothing to compare. */
      return next.size >= 2 ? next : current;
    });
  const rankByBot = useMemo(
    () => new Map(bots.map((bot, index) => [bot.bot_id, index + 1])),
    [bots],
  );
  const rows = useMemo(() => {
    if (!sortState) return bots;
    const resultAgainst = (botId: number) => {
      if (botId === sortState.botId) return 0;
      return matrixResult(botId, sortState.botId)?.adjusted_bb_per_hand ?? null;
    };
    return [...bots].sort((left, right) => {
      const leftResult = resultAgainst(left.bot_id);
      const rightResult = resultAgainst(right.bot_id);
      if (leftResult === null && rightResult === null) {
        return (
          (rankByBot.get(left.bot_id) ?? 0) - (rankByBot.get(right.bot_id) ?? 0)
        );
      }
      if (leftResult === null) return 1;
      if (rightResult === null) return -1;
      const result =
        sortState.direction === 'asc'
          ? leftResult - rightResult
          : rightResult - leftResult;
      return (
        result ||
        (rankByBot.get(left.bot_id) ?? 0) - (rankByBot.get(right.bot_id) ?? 0)
      );
    });
  }, [bots, rankByBot, sortState]);
  const minimumMatrixWidth = 165 + bots.length * 90;

  function setBot(botId: number, checked: boolean) {
    setEnabled((current) => {
      const next = new Set(current);
      if (checked) next.add(botId);
      else if (next.size > 2) next.delete(botId);
      return next;
    });
    if (!checked) {
      setFocusedRows((current) => {
        const next = new Set(current);
        next.delete(botId);
        return next;
      });
      setSortState((current) => (current?.botId === botId ? null : current));
    }
  }

  function toggleRow(botId: number) {
    setFocusedRows((current) => {
      const next = new Set(current);
      if (next.has(botId)) next.delete(botId);
      else next.add(botId);
      return next;
    });
  }

  function cycleSort(botId: number) {
    setSortState((current) => {
      if (current?.botId !== botId) return { botId, direction: 'desc' };
      if (current.direction === 'desc') return { botId, direction: 'asc' };
      return null;
    });
  }

  return (
    <main className="min-h-screen bg-[#f6f2e9] text-[#231f1b]">
      <div className="mx-auto max-w-[1500px] px-5 py-7 sm:px-8">
        <section className="mb-5 flex flex-col gap-4 border-b-4 border-[#27221e] pb-5 sm:flex-row sm:items-end sm:justify-between">
          <div>
            <h1 className="font-serif text-4xl font-medium tracking-tight sm:text-5xl">
              Matchup Matrix
            </h1>
            <p className="mt-2 text-sm text-[#695f55]">
              Adjusted bb/hand · ± shows a 95% confidence interval
            </p>
          </div>
          <div className="flex items-center gap-2 border border-[#a89f93] bg-[#fffdf8] px-3 py-2">
            <Checkbox
              id="margin-only-elo"
              checked={ratingFormula === 'margin-only'}
              onCheckedChange={(checked) =>
                setRatingFormula(checked ? 'margin-only' : 'outcome-first')
              }
              className="rounded-none border-[#756b60] data-checked:border-[#29231d] data-checked:bg-[#29231d]"
            />
            <label
              htmlFor="margin-only-elo"
              className="cursor-pointer text-sm font-medium"
              title="Ignores who won and reads only by how much: a hair-thin win counts for almost nothing, a blowout for a lot"
            >
              Order by amount won
            </label>
          </div>
        </section>

        <div className="matrix-scroll overflow-auto border border-[#332d27] bg-[#fffdf8] shadow-[8px_8px_0_#d9d0c3]">
          <table
            className="w-full table-fixed border-separate border-spacing-0 text-sm"
            style={{ minWidth: minimumMatrixWidth }}
          >
            <colgroup>
              <col style={{ width: 165 }} />
              {bots.map((bot) => (
                <col key={bot.bot_id} style={{ width: 90 }} />
              ))}
            </colgroup>
            <thead>
              <tr>
                <th className="sticky left-0 top-0 z-30 w-40 border-b border-r border-[#d8cfc2] bg-[#f0e9de] p-3 text-left text-xs font-medium">
                  BOT / OPPONENT
                </th>
                {bots.map((bot, index) => {
                  /* Same rule as the row names: the column keeps its tier
                   * colour and darkens a step while the pointer is anywhere
                   * in it, header or square. */
                  const columnBase = botGroup(bot.name)?.shade ?? '#eee7dc';
                  return (
                    <th
                      key={bot.bot_id}
                      style={{
                        backgroundColor:
                          hoveredColumn === bot.bot_id
                            ? shadeLit(columnBase)
                            : columnBase,
                      }}
                      aria-sort={
                        sortState?.botId === bot.bot_id
                          ? sortState.direction === 'asc'
                            ? 'ascending'
                            : 'descending'
                          : 'none'
                      }
                      className="sticky top-0 z-20 border-b border-r border-[#d8cfc2] bg-[#eee7dc] p-0 text-left align-bottom transition-colors"
                      onMouseEnter={() => setHoveredColumn(bot.bot_id)}
                      onMouseLeave={() =>
                        setHoveredColumn((current) =>
                          current === bot.bot_id ? null : current,
                        )
                      }
                      onFocus={() => setHoveredColumn(bot.bot_id)}
                      onBlur={() =>
                        setHoveredColumn((current) =>
                          current === bot.bot_id ? null : current,
                        )
                      }
                    >
                      <button
                        type="button"
                        onClick={() => cycleSort(bot.bot_id)}
                        className="flex min-h-[78px] w-full flex-col justify-end p-3 text-left focus-visible:outline-2 focus-visible:outline-[#bf2f25]"
                        aria-label={`Sort rows by result against ${bot.name}`}
                      >
                        <span className="flex w-full items-center gap-1.5">
                          <span
                            className="line-clamp-2 min-w-0 flex-1 break-words text-xs leading-tight font-medium whitespace-normal"
                            title={bot.name}
                          >
                            <span className="mr-1.5 font-mono text-[11px] font-bold text-[#231f1b]">
                              #{index + 1}
                            </span>
                            {bot.name}
                          </span>
                          {sortState?.botId === bot.bot_id && (
                            <span
                              className="font-mono text-xs text-[#756b60]"
                              aria-hidden="true"
                            >
                              {sortState.direction === 'asc' ? '↑' : '↓'}
                            </span>
                          )}
                        </span>
                        <span className="mt-0.5 block font-mono text-[11px] text-[#756b60]">
                          {bot.elo.toFixed(0)} Elo
                        </span>
                      </button>
                    </th>
                  );
                })}
              </tr>
            </thead>
            <tbody>
              {rows.map((rowBot) => {
                const focused = focusedRows.has(rowBot.bot_id);
                /* A row being pointed at is never dimmed, even while another
                 * row is latched -- otherwise the preview lands under 20%
                 * opacity and cannot be seen. */
                const dimmed =
                  focusedRows.size > 0 &&
                  !focused &&
                  hoveredRow !== rowBot.bot_id;
                const rank = rankByBot.get(rowBot.bot_id);
                const litRow = focused || hoveredRow === rowBot.bot_id;
                /* The tier's pastel neutral, behind the bot's name. */
                const group = botGroup(rowBot.name);
                /* Pointing at a square lights both lines it sits on, so a
                 * result in the middle of a wide table can be read back to
                 * the two bots it belongs to without moving the pointer. */
                const hoverCell = (columnBotId: number) => ({
                  onMouseEnter: () => {
                    setHoveredRow(rowBot.bot_id);
                    setHoveredColumn(columnBotId);
                  },
                  onMouseLeave: () => {
                    setHoveredRow((current) =>
                      current === rowBot.bot_id ? null : current,
                    );
                    setHoveredColumn((current) =>
                      current === columnBotId ? null : current,
                    );
                  },
                });
                /* The squares carry no tier colour, so there is nothing to
                 * preserve and a flat wash is the clearest thing to light
                 * them with: the one cool tone in a warm table, and the only
                 * green in the palette. */
                const lit = { backgroundColor: '#e8ece9' };
                /* The name keeps its tier colour whether or not the row is
                 * lit; being pointed at only takes it a step darker. */
                const base = group ? groupShade(group) : '#f0e9de';
                const rowCellStyle = {
                  backgroundColor: litRow ? shadeLit(base) : base,
                };
                const cellStyle = (columnBotId: number) =>
                  litRow || hoveredColumn === columnBotId ? lit : undefined;
                return (
                  <tr
                    key={rowBot.bot_id}
                    className={`h-[78px] transition-opacity ${dimmed ? 'opacity-20' : 'opacity-100'}`}
                  >
                    <th
                      className="sticky left-0 z-10 border-r border-t border-[#d8cfc2] bg-[#f0e9de] p-0 text-left transition-colors"
                      style={rowCellStyle}
                      onMouseEnter={() => setHoveredRow(rowBot.bot_id)}
                      onMouseLeave={() =>
                        setHoveredRow((current) =>
                          current === rowBot.bot_id ? null : current,
                        )
                      }
                      onFocus={() => setHoveredRow(rowBot.bot_id)}
                      onBlur={() =>
                        setHoveredRow((current) =>
                          current === rowBot.bot_id ? null : current,
                        )
                      }
                    >
                      <div className="relative h-[78px] p-3 transition-colors hover:bg-[#dfe5e1]">
                        <button
                          type="button"
                          onClick={() => toggleRow(rowBot.bot_id)}
                          aria-label={`${focused ? 'Unfocus' : 'Focus'} ${rowBot.name} row`}
                          aria-pressed={focused}
                          className="absolute inset-0 w-full focus-visible:outline-2 focus-visible:outline-[#6f7d74]"
                        />
                        <Link
                          href={`/bot/${rowBot.bot_id}`}
                          className="relative z-10 block truncate text-sm font-medium hover:underline focus-visible:outline-2 focus-visible:outline-[#bf2f25]"
                          title={rowBot.name}
                        >
                          <span className="mr-1.5 font-mono text-[11px] font-bold text-[#231f1b]">
                            #{rank}
                          </span>
                          {rowBot.name}
                        </Link>
                        <span className="pointer-events-none relative z-10 mt-0.5 block font-mono text-[11px] text-[#756b60]">
                          {rowBot.elo.toFixed(0)} Elo
                        </span>
                      </div>
                    </th>
                    {bots.map((columnBot) => {
                      if (rowBot.bot_id === columnBot.bot_id) {
                        return (
                          <td
                            key={columnBot.bot_id}
                            style={cellStyle(columnBot.bot_id)}
                            {...hoverCell(columnBot.bot_id)}
                            className="missing-cell h-[78px] border-r border-t border-[#e6ded3] p-2 text-center text-[#8a8074] transition-colors"
                          >
                            —
                          </td>
                        );
                      }
                      const result = matrixResult(
                        rowBot.bot_id,
                        columnBot.bot_id,
                      );
                      if (!result) {
                        return (
                          <td
                            key={columnBot.bot_id}
                            style={cellStyle(columnBot.bot_id)}
                            {...hoverCell(columnBot.bot_id)}
                            className="missing-cell h-[78px] border-r border-t border-[#e6ded3] p-2 text-center text-[#8a8074] transition-colors"
                          >
                            ·
                          </td>
                        );
                      }
                      return (
                        <td
                          key={columnBot.bot_id}
                          style={cellStyle(columnBot.bot_id)}
                          {...hoverCell(columnBot.bot_id)}
                          className="h-[78px] border-r border-t border-[#e6ded3] p-1.5 transition-colors"
                        >
                          <Link
                            href={`/matchup/${result.match_id}/${rowBot.bot_id}`}
                            style={resultTone(result.adjusted_bb_per_hand)}
                            className="flex h-[66px] flex-col justify-center rounded-sm px-3 transition hover:-translate-y-px hover:ring-2 hover:ring-[#29231d] focus-visible:ring-2 focus-visible:ring-[#29231d]"
                            aria-label={`${rowBot.name} versus ${columnBot.name}: ${signed(result.adjusted_bb_per_hand)} big blinds per hand, 95 percent confidence interval plus or minus ${(1.96 * result.adjusted_standard_error).toFixed(2)}`}
                          >
                            <span className="whitespace-nowrap font-mono text-[15px] font-semibold">
                              {signed(result.adjusted_bb_per_hand)}
                            </span>
                            <span className="mt-1 font-mono text-[11px] opacity-65">
                              ±
                              {(1.96 * result.adjusted_standard_error).toFixed(
                                2,
                              )}
                            </span>
                          </Link>
                        </td>
                      );
                    })}
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>

        <div className="mt-6">
          <DropdownMenu>
            <DropdownMenuTrigger className="flex min-w-52 items-center justify-between border border-[#29231d] bg-[#fffdf8] px-3 py-2 text-sm font-semibold outline-none hover:bg-[#eee7dc] focus-visible:ring-2 focus-visible:ring-[#bf2f25]">
              Compare · {bots.length} bots
              <ChevronDown className="size-4" aria-hidden="true" />
            </DropdownMenuTrigger>
            <DropdownMenuContent
              align="start"
              className="w-auto rounded-none border border-[#29231d] bg-[#fffdf8] p-0 text-[#231f1b] shadow-[6px_6px_0_#d9d0c3] ring-0"
            >
              <div className="flex items-start">
                <DropdownMenuGroup className="min-w-52 border-r border-[#e3dbd0]">
                  <DropdownMenuLabel>Select bots</DropdownMenuLabel>
                  <DropdownMenuSeparator className="bg-[#d8cfc2]" />
                  {roster.map((bot, index) => {
                    const group = botGroup(bot.name);
                    const opensTier =
                      index > 0 &&
                      botGroupIndex(bot.name) !==
                        botGroupIndex(roster[index - 1].name);
                    return (
                      <Fragment key={bot.bot_id}>
                        {opensTier && (
                          <DropdownMenuSeparator className="bg-[#ece5da]" />
                        )}
                        <DropdownMenuCheckboxItem
                          checked={enabled.has(bot.bot_id)}
                          disabled={
                            enabled.has(bot.bot_id) && enabled.size <= 2
                          }
                          onCheckedChange={(checked) =>
                            setBot(bot.bot_id, checked)
                          }
                          className="rounded-none focus:bg-[#eee7dc] focus:text-[#231f1b]"
                          style={
                            group
                              ? { backgroundColor: groupShade(group) }
                              : undefined
                          }
                        >
                          {bot.name}
                        </DropdownMenuCheckboxItem>
                      </Fragment>
                    );
                  })}
                </DropdownMenuGroup>
                <DropdownMenuGroup className="min-w-44">
                  <DropdownMenuLabel>By tier</DropdownMenuLabel>
                  <DropdownMenuSeparator className="bg-[#d8cfc2]" />
                  {groups.map((group) => {
                    const on = group.botIds.filter((id) => enabled.has(id));
                    return (
                      <DropdownMenuCheckboxItem
                        key={group.id}
                        checked={on.length === group.botIds.length}
                        onCheckedChange={(checked) =>
                          setGroup(group.botIds, checked)
                        }
                        title={group.note}
                        className="rounded-none focus:bg-[#eee7dc] focus:text-[#231f1b]"
                        style={{ backgroundColor: groupShade(group) }}
                      >
                        <span className="flex w-full items-baseline justify-between gap-2">
                          <span>{group.name}</span>
                          <span className="font-mono text-[10px] text-[#8b8177]">
                            {on.length}/{group.botIds.length}
                          </span>
                        </span>
                      </DropdownMenuCheckboxItem>
                    );
                  })}
                </DropdownMenuGroup>
              </div>
            </DropdownMenuContent>
          </DropdownMenu>
        </div>

        <p className="mt-5 font-serif text-sm italic text-[#756b60]">
          Each square is the row bot&apos;s adjusted result against the column
          bot. Select a square for the full matchup report.
        </p>

        <div className="mt-7 border-t border-[#d8cfc2] pt-4">
          <Link
            href="/preflop"
            className="font-mono text-xs font-semibold uppercase tracking-[0.08em] text-[#756b60] hover:text-[#b42c23] focus-visible:outline-2 focus-visible:outline-[#bf2f25]"
          >
            View preflop charts →
          </Link>
        </div>
      </div>
    </main>
  );
}
