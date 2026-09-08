'use client';

import Link from 'next/link';
import { useCallback, useEffect, useMemo, useState } from 'react';

import {
  type Frame,
  type HandDetail,
  type HandPlayer,
  STREETS,
  actionLabel,
  buildFrames,
  callPricePercent,
  fetchHand,
  handApi,
  legalActionNames,
} from '@/lib/hands';

const SUIT_GLYPHS: Record<string, string> = { c: '♣', d: '♦', s: '♠', h: '♥' };

function Card({ card, size = 'md' }: { card?: string; size?: 'sm' | 'md' | 'lg' }) {
  const box =
    size === 'lg'
      ? 'h-16 w-12 text-lg'
      : size === 'sm'
        ? 'h-7 w-6 text-[11px]'
        : 'h-11 w-8 text-sm';
  if (!card) {
    return (
      <span
        className={`inline-flex ${box} items-center justify-center border border-dashed border-[#cfc4b6] bg-[#f1ece2]`}
      />
    );
  }
  const red = card.endsWith('d') || card.endsWith('h');
  return (
    <span
      className={`inline-flex ${box} flex-col items-center justify-center border border-[#b3a89a] bg-white font-mono leading-none shadow-[1px_1px_0_#ddd4c7] ${
        red ? 'text-[#b52d24]' : 'text-[#241f1b]'
      }`}
    >
      <span className="font-semibold">{card.slice(0, -1)}</span>
      <span>{SUIT_GLYPHS[card.slice(-1)]}</span>
    </span>
  );
}

function Chips({ chips, bigBlind }: { chips: number; bigBlind: number }) {
  if (chips <= 0) return null;
  return (
    <span className="inline-block border border-[#c8ae6a] bg-[#f6ecd2] px-2 py-0.5 font-mono text-xs text-[#6b5320]">
      {(chips / bigBlind).toFixed(1)} BB
    </span>
  );
}

function StackBar({ stack, starting }: { stack: number; starting: number }) {
  const share = Math.max(0, Math.min(1, stack / starting));
  return (
    <span className="inline-block h-1.5 w-28 border border-[#cfc4b6] bg-[#f1ece2] align-middle">
      <span className="block h-full bg-[#7d8f6f]" style={{ width: `${share * 100}%` }} />
    </span>
  );
}

function Seat({
  player,
  frame,
  starting,
  bigBlind,
  acting,
  large,
  showEquity,
}: {
  player: HandPlayer;
  frame: Frame;
  starting: number;
  bigBlind: number;
  acting: boolean;
  large: boolean;
  showEquity: boolean;
}) {
  const stack = frame.stacks[player.position];
  return (
    <div
      className={`flex items-center gap-4 border p-4 transition-colors ${
        acting ? 'border-[#29231d] bg-[#fffdf8]' : 'border-[#ded5c9] bg-[#fbf8f1]'
      }`}
    >
      <div className="flex gap-1">
        {[0, 1].map((index) => (
          <Card key={index} card={player.hole[index]} size={large ? 'lg' : 'md'} />
        ))}
      </div>
      <div className="min-w-0 flex-1">
        <p className="truncate font-serif text-xl">
          <Link href={`/bot/${player.bot_id}`} className="hover:underline">
            {player.name}
          </Link>
          <span className="ml-2 font-mono text-[10px] uppercase tracking-[.08em] text-[#8b8177]">
            {player.position === 0 ? 'button · sb' : 'big blind'}
          </span>
        </p>
        <p className="mt-1.5 flex items-center gap-2">
          <StackBar stack={stack} starting={starting} />
          <span className="font-mono text-xs text-[#756a60]">
            {(stack / bigBlind).toFixed(1)} BB behind
          </span>
        </p>
      </div>
      <div className="text-right">
        {showEquity && player.exact_equity !== null ? (
          <span className="block font-mono text-sm text-[#4a423b]">
            {(player.exact_equity * 100).toFixed(1)}%
            <span className="ml-1 text-[10px] uppercase tracking-[.07em] text-[#8b8177]">
              equity
            </span>
          </span>
        ) : (
          <Chips chips={frame.streetContribution[player.position]} bigBlind={bigBlind} />
        )}
      </div>
    </div>
  );
}

export function HandReplay({
  matchId,
  handIndex,
}: {
  matchId: number;
  handIndex: number;
}) {
  const [hand, setHand] = useState<HandDetail | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [step, setStep] = useState(0);
  const [showDetail, setShowDetail] = useState(false);

  useEffect(() => {
    setHand(null);
    setStep(0);
    fetchHand(matchId, handIndex)
      .then(setHand)
      .catch((cause: Error) => setError(cause.message));
  }, [matchId, handIndex]);

  const frames = useMemo(() => (hand ? buildFrames(hand) : []), [hand]);

  const move = useCallback(
    (delta: number) =>
      setStep((current) => Math.max(0, Math.min(frames.length - 1, current + delta))),
    [frames.length],
  );

  useEffect(() => {
    if (frames.length === 0) return;
    const onKey = (event: KeyboardEvent) => {
      if (event.target instanceof HTMLInputElement) return;
      if (event.key === 'ArrowRight' || event.key === ' ') {
        event.preventDefault();
        move(1);
      } else if (event.key === 'ArrowLeft') {
        event.preventDefault();
        move(-1);
      } else if (event.key === 'Home') {
        setStep(0);
      } else if (event.key === 'End') {
        setStep(frames.length - 1);
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [frames.length, move]);

  if (error) {
    return (
      <section className="border border-[#cfc4b6] bg-[#fffdf8] p-8">
        <h2 className="font-serif text-2xl">That hand could not be loaded</h2>
        <p className="mt-3 text-sm text-[#5c534b]">
          {error} — tried {handApi()}. The replay needs{' '}
          <code>scripts/hand_server.py</code> running on the machine with the ledger.
        </p>
      </section>
    );
  }
  if (!hand) {
    return <p className="py-16 text-center text-sm text-[#8b8177]">loading…</p>;
  }

  const frame = frames[step];
  const decision = frame.decision;
  const bigBlind = hand.summary.big_blind;
  const starting = hand.summary.starting_stack;
  const byPosition = [...hand.players].sort((a, b) => a.position - b.position);
  const actor = frame.actor === null ? null : byPosition[frame.actor];
  const hero = hand.players[0];
  const villain = hand.players[1];
  /* During the runout the hand is decided by cards rather than choices, so
   * the seats show what each hand was worth instead of what is in front of
   * it. Not before: a preflop all-in would otherwise show the equity while
   * the betting was still going on. */
  const allIn = frame.runout;

  const field = (label: string, value: string, muted = false) => (
    <div key={label} className="flex items-baseline justify-between gap-4 py-1.5">
      <span className="text-[10px] uppercase tracking-[.07em] text-[#8b8177]">{label}</span>
      <span className={`font-mono text-sm ${muted ? 'text-[#8b8177]' : 'text-[#241f1b]'}`}>
        {value}
      </span>
    </div>
  );

  return (
    <>
      <section className="mb-5">
        <h1 className="font-serif text-3xl">
          {hero.name} <span className="text-[#8b8177]">vs</span> {villain.name}
        </h1>
        <p className="mt-1 font-mono text-xs uppercase tracking-[.08em] text-[#756a60]">
          match {hand.match_id} · hand {hand.hand_index.toLocaleString()} ·{' '}
          {hand.summary.pot_class.replace(/_/g, ' ')}
        </p>
      </section>

      <div className="grid gap-5 lg:grid-cols-[1fr_320px]">
        <div className="space-y-3">
          <Seat
            player={byPosition[1]}
            frame={frame}
            starting={starting}
            bigBlind={bigBlind}
            acting={frame.actor === 1}
            large={false}
            showEquity={allIn}
          />
          <div className="flex items-center justify-between gap-6 border border-[#cfc4b6] bg-[#eef1e7] px-5 py-6">
            <div className="flex gap-1.5">
              {Array.from({ length: 5 }, (_, index) => (
                <Card
                  key={index}
                  card={index < frame.boardCount ? hand.board[index] : undefined}
                />
              ))}
            </div>
            <div className="text-right">
              <span className="block text-[10px] uppercase tracking-[.07em] text-[#6f7563]">
                {STREETS[frame.street]} pot
              </span>
              <strong className="font-mono text-2xl font-normal">
                {(frame.pot / bigBlind).toFixed(1)} BB
              </strong>
            </div>
          </div>
          <Seat
            player={byPosition[0]}
            frame={frame}
            starting={starting}
            bigBlind={bigBlind}
            acting={frame.actor === 0}
            large
            showEquity={allIn}
          />
        </div>

        {/* Empty until someone has actually acted. The action is the whole of
            it; the state that produced it is there for whoever wants it. */}
        <aside className="min-h-[13rem] border border-[#cfc4b6] bg-[#fffdf8] p-5">
          {frame.runout && (
            <>
              <strong className="block font-serif text-3xl font-normal">
                Runout
              </strong>
              <table className="mt-4 w-full border-collapse text-sm">
                <tbody>
                  {hand.players.map((player) => (
                    <tr key={player.bot_slot}>
                      <td className="py-1.5 pr-3">{player.name}</td>
                      <td className="py-1.5 pr-3 text-right font-mono">
                        {player.exact_equity === null
                          ? '—'
                          : `${(player.exact_equity * 100).toFixed(1)}%`}
                      </td>
                      <td
                        className={`py-1.5 text-right font-mono ${
                          player.raw_net_chips > 0
                            ? 'text-[#087343]'
                            : player.raw_net_chips < 0
                              ? 'text-[#b52d24]'
                              : 'text-[#756a60]'
                        }`}
                      >
                        {player.raw_net_chips > 0 ? '+' : ''}
                        {(player.raw_net_chips / bigBlind).toFixed(0)} BB
                      </td>
                    </tr>
                  ))}
                  <tr>
                    <td />
                    <td className="pt-1 pr-3 text-right text-[10px] uppercase tracking-[.07em] text-[#8b8177]">
                      equity
                    </td>
                    <td className="pt-1 text-right text-[10px] uppercase tracking-[.07em] text-[#8b8177]">
                      actual
                    </td>
                  </tr>
                </tbody>
              </table>
              <p className="mt-3 text-xs text-[#8b8177]">
                The adjusted result uses the equity, not this runout.
              </p>
            </>
          )}
          {decision && (
            <>
              <div className="flex items-baseline justify-between gap-3">
                <p className="font-mono text-[10px] uppercase tracking-[.08em] text-[#8b8177]">
                  {actor?.name}
                </p>
                <p className="font-mono text-[10px] uppercase tracking-[.08em] text-[#8b8177]">
                  {STREETS[decision.street]}
                </p>
              </div>
              <strong className="mt-1 block font-mono text-2xl font-normal">
                {actionLabel(decision, bigBlind, frame.priorAggression)}
              </strong>
              <button
                type="button"
                onClick={() => setShowDetail((current) => !current)}
                className="mt-4 border border-[#cfc4b6] px-3 py-1.5 text-[11px] uppercase tracking-[.07em] text-[#756a60] hover:bg-[#f6f2e9]"
              >
                {showDetail ? 'Hide details' : 'Details'}
              </button>
              {showDetail && (
                <div className="mt-3 divide-y divide-[#f0eae0] border-t border-[#f0eae0] pt-1">
                  {field('Pot', `${(decision.pot / bigBlind).toFixed(1)} BB`)}
                  {field(
                    'To call',
                    decision.to_call > 0
                      ? `${(decision.to_call / bigBlind).toFixed(1)} BB`
                      : 'nothing',
                  )}
                  {field(
                    'Price',
                    decision.to_call > 0
                      ? `${callPricePercent(decision).toFixed(1)}% of the pot`
                      : '—',
                    decision.to_call === 0,
                  )}
                  {field('Its stack', `${(decision.my_stack / bigBlind).toFixed(1)} BB`)}
                  {field('Their stack', `${(decision.opp_stack / bigBlind).toFixed(1)} BB`)}
                  {field(
                    'Raise range',
                    decision.legal_actions & 8
                      ? `${(decision.min_raise_to / bigBlind).toFixed(1)} – ${(decision.max_raise_to / bigBlind).toFixed(1)} BB`
                      : 'cannot raise',
                    !(decision.legal_actions & 8),
                  )}
                  {field('Legal', legalActionNames(decision.legal_actions).join(', '))}
                  {field('Decided in', `${(decision.cpu_time_ns / 1000).toFixed(0)} µs`, true)}
                </div>
              )}
              {decision.violation !== 0 && (
                <p className="mt-3 border border-[#e0b4ae] bg-[#fdf3f1] p-3 text-xs text-[#b52d24]">
                  Illegal request; the harness substituted the action above.
                </p>
              )}
            </>
          )}
        </aside>
      </div>

      <section className="mt-5 flex flex-wrap items-center justify-between gap-4 border border-[#cfc4b6] bg-[#fffdf8] px-4 py-3">
        <div className="flex items-center gap-2">
          <button
            type="button"
            onClick={() => setStep(0)}
            className="border border-[#cfc4b6] px-3 py-1.5 text-xs hover:bg-[#f6f2e9]"
          >
            ⟵ Start
          </button>
          <button
            type="button"
            onClick={() => move(-1)}
            disabled={step === 0}
            className="border border-[#cfc4b6] px-4 py-1.5 text-sm hover:bg-[#f6f2e9] disabled:opacity-35"
          >
            ◀
          </button>
          <span className="w-24 text-center font-mono text-xs text-[#756a60]">
            {step} / {frames.length - 1}
          </span>
          <button
            type="button"
            onClick={() => move(1)}
            disabled={step === frames.length - 1}
            className="border border-[#cfc4b6] px-4 py-1.5 text-sm hover:bg-[#f6f2e9] disabled:opacity-35"
          >
            ▶
          </button>
        </div>
        <p className="font-mono text-xs">
          {hand.summary.showdown === 1
            ? 'showdown'
            : `${byPosition[hand.summary.folded_position ?? 0]?.name} folded`}
          <span className="mx-2 text-[#cfc4b6]">·</span>
          <span
            className={
              hero.raw_net_chips > 0
                ? 'text-[#087343]'
                : hero.raw_net_chips < 0
                  ? 'text-[#b52d24]'
                  : ''
            }
          >
            {hero.name} {hero.raw_net_chips > 0 ? '+' : ''}
            {(hero.raw_net_chips / bigBlind).toFixed(1)} BB
          </span>
        </p>
      </section>
    </>
  );
}
