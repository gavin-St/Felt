'use client';

import Link from 'next/link';
import { useMemo, useState } from 'react';
import { ChevronDown } from 'lucide-react';

import {
  DropdownMenu,
  DropdownMenuCheckboxItem,
  DropdownMenuContent,
  DropdownMenuLabel,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from '@/components/ui/dropdown-menu';
import { dashboard, matrixResult, resultTone, signed, subsetRatings } from '@/lib/dashboard';
import { BOT_PROFILES } from '@/lib/bots';
import { BotGlyph } from '@/components/bot-glyph';

type SortState = {
  botId: number;
  direction: 'asc' | 'desc';
} | null;

export function Scorecard() {
  const [enabled, setEnabled] = useState(
    () => new Set(dashboard.ratings.map((bot) => bot.bot_id)),
  );
  const [focusedRows, setFocusedRows] = useState(() => new Set<number>());
  const [sortState, setSortState] = useState<SortState>(null);
  const bots = useMemo(() => subsetRatings(enabled), [enabled]);
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
        return (rankByBot.get(left.bot_id) ?? 0) - (rankByBot.get(right.bot_id) ?? 0);
      }
      if (leftResult === null) return 1;
      if (rightResult === null) return -1;
      const result =
        sortState.direction === 'asc'
          ? leftResult - rightResult
          : rightResult - leftResult;
      return result || (rankByBot.get(left.bot_id) ?? 0) - (rankByBot.get(right.bot_id) ?? 0);
    });
  }, [bots, rankByBot, sortState]);

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
      setSortState((current) => current?.botId === botId ? null : current);
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
      if (current?.botId !== botId) return { botId, direction: 'asc' };
      if (current.direction === 'asc') return { botId, direction: 'desc' };
      return null;
    });
  }

  return (
    <main className="min-h-screen bg-[#f6f2e9] text-[#231f1b]">
      <div className="mx-auto max-w-[1500px] px-5 py-7 sm:px-8">
        <section className="mb-5 border-b-4 border-[#27221e] pb-5">
          <div>
            <h1 className="font-serif text-4xl font-medium tracking-tight sm:text-5xl">
              Head-to-head scorecard
            </h1>
            <p className="mt-2 text-sm text-[#695f55]">
              Adjusted bb/hand · ± shows a 95% confidence interval
            </p>
          </div>
        </section>

        <div className="matrix-scroll overflow-auto border border-[#332d27] bg-[#fffdf8] shadow-[8px_8px_0_#d9d0c3]">
          <table className="w-full min-w-[760px] table-fixed border-separate border-spacing-0 text-sm">
            <colgroup>
              <col style={{ width: 165 }} />
              {bots.map((bot) => <col key={bot.bot_id} />)}
            </colgroup>
            <thead>
              <tr>
                <th className="sticky left-0 z-20 w-40 border-b border-r border-[#d8cfc2] bg-[#f0e9de] p-3 text-left text-xs font-medium">
                  BOT / OPPONENT
                </th>
                {bots.map((bot, index) => (
                  <th
                    key={bot.bot_id}
                    aria-sort={
                      sortState?.botId === bot.bot_id
                        ? sortState.direction === 'asc' ? 'ascending' : 'descending'
                        : 'none'
                    }
                    className="border-b border-r border-[#d8cfc2] bg-[#eee7dc] p-0 text-left align-bottom"
                  >
                    <button
                      type="button"
                      onClick={() => cycleSort(bot.bot_id)}
                      className="flex h-[66px] w-full flex-col justify-end p-3 text-left hover:bg-[#e4dccf] focus-visible:outline-2 focus-visible:outline-[#bf2f25]"
                      aria-label={`Sort rows by result against ${bot.name}`}
                    >
                      <span className="flex w-full items-center gap-1.5">
                        <BotGlyph
                          glyph={BOT_PROFILES[bot.name]?.glyph ?? 'die'}
                          color={BOT_PROFILES[bot.name]?.color ?? '#756b60'}
                          size={13}
                        />
                        <span className="min-w-0 flex-1 truncate text-xs font-medium" title={bot.name}>
                          <span className="mr-1.5 font-mono text-[11px] font-semibold text-[#b42c23]">
                            #{index + 1}
                          </span>
                          {bot.name}
                        </span>
                        {sortState?.botId === bot.bot_id && (
                          <span className="font-mono text-xs text-[#756b60]" aria-hidden="true">
                            {sortState.direction === 'asc' ? '↑' : '↓'}
                          </span>
                        )}
                      </span>
                      <span className="mt-0.5 block font-mono text-[11px] text-[#756b60]">
                        {bot.elo.toFixed(0)} Elo
                      </span>
                    </div>
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {rows.map((rowBot) => {
                const focused = focusedRows.has(rowBot.bot_id);
                const dimmed = focusedRows.size > 0 && !focused;
                const rank = rankByBot.get(rowBot.bot_id);
                const focusedCellStyle = focused ? { backgroundColor: '#e8ece9' } : undefined;
                return (
                <tr
                  key={rowBot.bot_id}
                  className={`h-[78px] transition-opacity ${dimmed ? 'opacity-20' : 'opacity-100'}`}
                >
                  <th
                    className="sticky left-0 z-10 border-r border-t border-[#d8cfc2] bg-[#f0e9de] p-0 text-left transition-colors"
                    style={focusedCellStyle}
                  >
                    <div
                      role="button"
                      tabIndex={0}
                      onClick={() => toggleRow(rowBot.bot_id)}
                      onKeyDown={(event) => {
                        if (event.key === 'Enter' || event.key === ' ') {
                          event.preventDefault();
                          toggleRow(rowBot.bot_id);
                        }
                      }}
                      aria-pressed={focused}
                      className="h-[78px] w-full cursor-pointer p-3 text-left transition-colors hover:bg-[#dfe5e1] focus-visible:outline-2 focus-visible:outline-[#6f7d74]"
                    >
                      <span className="flex items-center gap-1.5 truncate text-sm font-medium" title={rowBot.name}>
                        <span className="font-mono text-[11px] font-semibold text-[#b42c23]">
                          #{rank}
                        </span>
                        <BotGlyph
                          glyph={BOT_PROFILES[rowBot.name]?.glyph ?? 'die'}
                          color={BOT_PROFILES[rowBot.name]?.color ?? '#756b60'}
                          size={14}
                        />
                        <Link
                          href={`/bot/${rowBot.bot_id}`}
                          onClick={(event) => event.stopPropagation()}
                          className="truncate underline decoration-[#c4b9a9] underline-offset-2 hover:decoration-[#231f1b]"
                        >
                          {BOT_PROFILES[rowBot.name]?.character ?? rowBot.name}
                        </Link>
                      </span>
                      <span className="mt-0.5 block font-mono text-[11px] text-[#756b60]">
                        {rowBot.elo.toFixed(0)} Elo
                      </span>
                    </div>
                  </th>
                  {bots.map((columnBot) => {
                    if (rowBot.bot_id === columnBot.bot_id) {
                      return (
                        <td key={columnBot.bot_id} style={focusedCellStyle} className="missing-cell h-[78px] border-r border-t border-[#e6ded3] p-2 text-center text-[#8a8074] transition-colors">
                          —
                        </td>
                      );
                    }
                    const result = matrixResult(rowBot.bot_id, columnBot.bot_id);
                    if (!result) {
                      return (
                        <td key={columnBot.bot_id} style={focusedCellStyle} className="missing-cell h-[78px] border-r border-t border-[#e6ded3] p-2 text-center text-[#8a8074] transition-colors">
                          ·
                        </td>
                      );
                    }
                    return (
                      <td key={columnBot.bot_id} style={focusedCellStyle} className="h-[78px] border-r border-t border-[#e6ded3] p-1.5 transition-colors">
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
                            ±{(1.96 * result.adjusted_standard_error).toFixed(2)}
                          </span>
                        </Link>
                      </td>
                    );
                  })}
                </tr>
              )})}
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
              className="w-60 rounded-none border border-[#29231d] bg-[#fffdf8] text-[#231f1b] shadow-[6px_6px_0_#d9d0c3] ring-0"
            >
              <DropdownMenuLabel>Select bots</DropdownMenuLabel>
              <DropdownMenuSeparator className="bg-[#d8cfc2]" />
              {dashboard.ratings.map((bot) => (
                <DropdownMenuCheckboxItem
                  key={bot.bot_id}
                  checked={enabled.has(bot.bot_id)}
                  disabled={enabled.has(bot.bot_id) && enabled.size <= 2}
                  onCheckedChange={(checked) => setBot(bot.bot_id, checked)}
                  className="rounded-none focus:bg-[#eee7dc] focus:text-[#231f1b]"
                >
                  {bot.name}
                </DropdownMenuCheckboxItem>
              ))}
            </DropdownMenuContent>
          </DropdownMenu>
        </div>

        <p className="mt-5 font-serif text-sm italic text-[#756b60]">
          Each square is the row bot&apos;s adjusted result against the column bot. Select a square
          for the full matchup report, or a bot&apos;s name for its profile.
        </p>
      </div>
    </main>
  );
}
