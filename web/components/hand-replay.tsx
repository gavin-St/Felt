'use client';

import Link from 'next/link';
import { useCallback, useEffect, useMemo, useState } from 'react';

import {
  ACTION_NAMES,
  type Frame,
  type HandDetail,
  type HandPlayer,
  STREETS,
  buildFrames,
  callPricePercent,
  describeEvent,
  fetchHand,
  handApi,
  isAggressive,
  legalActionNames,
} from '@/lib/hands';

type View = 'inspector' | 'timeline';

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
      <span
        className="block h-full bg-[#7d8f6f]"
        style={{ width: `${share * 100}%` }}
      />
    </span>
  );
}

function Seat({
  player,
  frame,
  starting,
  bigBlind,
  acting,
  align,
}: {
  player: HandPlayer;
  frame: Frame;
  starting: number;
  bigBlind: number;
  acting: boolean;
  align: 'top' | 'bottom';
}) {
  const stack = frame.stacks[player.position];
  const bet = frame.streetContribution[player.position];
  return (
    <div
      className={`flex items-center gap-4 border p-4 transition-colors ${
        acting ? 'border-[#29231d] bg-[#fffdf8]' : 'border-[#ded5c9] bg-[#fbf8f1]'
      }`}
    >
      <div className="flex gap-1">
        {[0, 1].map((index) => (
          <Card key={index} card={player.hole[index]} size={align === 'bottom' ? 'lg' : 'md'} />
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
        <Chips chips={bet} bigBlind={bigBlind} />
      </div>
    </div>
  );
}

function Board({ cards, count }: { cards: string[]; count: number }) {
  return (
    <div className="flex gap-1.5">
      {Array.from({ length: 5 }, (_, index) => (
        <Card key={index} card={index < count ? cards[index] : undefined} />
      ))}
    </div>
  );
}

/* ---------------------------------------------------------------- view A */

function Inspector({
  hand,
  frames,
  step,
}: {
  hand: HandDetail;
  frames: Frame[];
  step: number;
}) {
  const frame = frames[step];
  const bigBlind = hand.summary.big_blind;
  const starting = hand.summary.starting_stack;
  const byPosition = [...hand.players].sort((a, b) => a.position - b.position);
  const decision = frame.decision;
  const actor = frame.actor === null ? null : byPosition[frame.actor];

  const field = (label: string, value: string, muted = false) => (
    <div key={label} className="flex items-baseline justify-between gap-4 py-1.5">
      <span className="text-[10px] uppercase tracking-[.07em] text-[#8b8177]">
        {label}
      </span>
      <span
        className={`font-mono text-sm ${muted ? 'text-[#8b8177]' : 'text-[#241f1b]'}`}
      >
        {value}
      </span>
    </div>
  );

  return (
    <div className="grid gap-5 lg:grid-cols-[1fr_320px]">
      <div className="space-y-3">
        <Seat
          player={byPosition[1]}
          frame={frame}
          starting={starting}
          bigBlind={bigBlind}
          acting={frame.actor === 1}
          align="top"
        />
        <div className="flex items-center justify-between gap-6 border border-[#cfc4b6] bg-[#eef1e7] px-5 py-6">
          <Board cards={hand.board} count={frame.boardCount} />
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
          align="bottom"
        />
      </div>

      <aside className="border border-[#cfc4b6] bg-[#fffdf8] p-5">
        <h3 className="font-serif text-lg">
          {step === 0
            ? 'Cards are out'
            : decision
              ? `What ${actor?.name} saw`
              : `${actor?.name} posts`}
        </h3>
        {step === 0 && (
          <p className="mt-2 text-sm text-[#5c534b]">
            Blinds are 0.5 and 1 BB, both stacks start at{' '}
            {(starting / bigBlind).toFixed(0)} BB. Step forward to walk the hand.
          </p>
        )}
        {frame.event && !decision && (
          <p className="mt-2 text-sm text-[#5c534b]">
            {describeEvent(frame.event, bigBlind)} — a blind is posted, not
            chosen, so the bot was never asked.
          </p>
        )}
        {decision && (
          <>
            <div className="mt-3 divide-y divide-[#f0eae0]">
              {field('Street', STREETS[decision.street])}
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
            <div className="mt-4 border border-[#29231d] bg-[#29231d] px-4 py-3 text-[#f6f2e9]">
              <span className="block text-[10px] uppercase tracking-[.07em] text-[#b9b0a4]">
                It chose
              </span>
              <strong className="font-mono text-lg font-normal">
                {ACTION_NAMES[decision.applied.type]}
                {decision.applied.type === 4 &&
                  ` ${(decision.applied.amount_to / bigBlind).toFixed(1)} BB`}
              </strong>
            </div>
            {decision.violation !== 0 && (
              <p className="mt-3 border border-[#e0b4ae] bg-[#fdf3f1] p-3 text-xs text-[#b52d24]">
                Illegal request {ACTION_NAMES[decision.requested.type]}{' '}
                {decision.requested.amount_to}; the harness substituted the action
                above.
              </p>
            )}
          </>
        )}
      </aside>
    </div>
  );
}

/* ---------------------------------------------------------------- view B */

function Timeline({
  hand,
  frames,
  step,
  onStep,
}: {
  hand: HandDetail;
  frames: Frame[];
  step: number;
  onStep: (next: number) => void;
}) {
  const bigBlind = hand.summary.big_blind;
  const frame = frames[step];
  const byPosition = [...hand.players].sort((a, b) => a.position - b.position);
  const biggest = Math.max(...frames.map((item) => item.pot), bigBlind);

  return (
    <div className="space-y-4">
      <div className="flex flex-wrap items-center justify-between gap-6 border border-[#cfc4b6] bg-[#eef1e7] px-5 py-4">
        <Board cards={hand.board} count={frame.boardCount} />
        <div className="flex gap-8">
          {byPosition.map((player) => (
            <div key={player.bot_slot} className="text-right">
              <span className="block text-[10px] uppercase tracking-[.07em] text-[#6f7563]">
                {player.name}
              </span>
              <span className="font-mono text-sm">
                {(frame.stacks[player.position] / bigBlind).toFixed(1)} BB
              </span>
              <span className="ml-2 inline-flex gap-0.5 align-middle">
                {player.hole.map((card) => (
                  <Card key={card} card={card} size="sm" />
                ))}
              </span>
            </div>
          ))}
          <div className="text-right">
            <span className="block text-[10px] uppercase tracking-[.07em] text-[#6f7563]">
              Pot
            </span>
            <strong className="font-mono text-xl font-normal">
              {(frame.pot / bigBlind).toFixed(1)} BB
            </strong>
          </div>
        </div>
      </div>

      <input
        type="range"
        min={0}
        max={frames.length - 1}
        value={step}
        onChange={(event) => onStep(Number(event.target.value))}
        className="w-full accent-[#29231d]"
        aria-label="Step through the hand"
      />

      <ol className="border border-[#cfc4b6] bg-[#fffdf8]">
        {frames.map((item, index) => {
          const newStreet =
            index > 0 && item.street !== frames[index - 1].street;
          const actor = item.actor === null ? null : byPosition[item.actor];
          const current = index === step;
          const future = index > step;
          return (
            <li key={item.step}>
              {(newStreet || index === 0) && (
                <p className="border-y border-[#e3dbd0] bg-[#f6f2e9] px-4 py-1.5 font-mono text-[10px] uppercase tracking-[.08em] text-[#756a60]">
                  {STREETS[item.street]}
                  {item.boardCount > 0 && (
                    <span className="ml-3">
                      {hand.board.slice(0, item.boardCount).join(' ')}
                    </span>
                  )}
                </p>
              )}
              <button
                type="button"
                onClick={() => onStep(index)}
                className={`flex w-full items-center gap-4 px-4 py-2 text-left transition-colors ${
                  current ? 'bg-[#29231d] text-[#f6f2e9]' : 'hover:bg-[#f6f2e9]'
                } ${future ? 'opacity-40' : ''}`}
              >
                <span className="w-6 font-mono text-[10px] opacity-60">
                  {index}
                </span>
                <span className="w-40 truncate text-sm">
                  {index === 0 ? 'cards are dealt' : actor?.name}
                </span>
                <span
                  className={`w-44 font-mono text-xs ${
                    item.event && isAggressive(item.event) && !current
                      ? 'text-[#b52d24]'
                      : ''
                  }`}
                >
                  {item.event ? describeEvent(item.event, bigBlind) : '—'}
                </span>
                <span className="flex flex-1 items-center gap-2">
                  <span
                    className={`inline-block h-2 ${current ? 'bg-[#d8cdb8]' : 'bg-[#cfc4b6]'}`}
                    style={{ width: `${(item.pot / biggest) * 100}%` }}
                  />
                  <span className="font-mono text-[11px] opacity-70">
                    {(item.pot / bigBlind).toFixed(1)}
                  </span>
                </span>
              </button>
            </li>
          );
        })}
      </ol>
    </div>
  );
}

/* ----------------------------------------------------------------- shell */

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
  const [view, setView] = useState<View>('inspector');

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
      setStep((current) =>
        Math.max(0, Math.min(frames.length - 1, current + delta)),
      ),
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
          <code>scripts/hand_server.py</code> running on the machine with the
          ledger.
        </p>
      </section>
    );
  }
  if (!hand) {
    return <p className="py-16 text-center text-sm text-[#8b8177]">loading…</p>;
  }

  const hero = hand.players[0];
  const villain = hand.players[1];
  const bigBlind = hand.summary.big_blind;
  const last = step === frames.length - 1;

  return (
    <>
      <section className="mb-5 flex flex-wrap items-center justify-between gap-4">
        <div>
          <h1 className="font-serif text-3xl">
            {hero.name} <span className="text-[#8b8177]">vs</span> {villain.name}
          </h1>
          <p className="mt-1 font-mono text-xs uppercase tracking-[.08em] text-[#756a60]">
            match {hand.match_id} · hand {hand.hand_index.toLocaleString()} ·{' '}
            {hand.summary.pot_class.replace(/_/g, ' ')}
          </p>
        </div>
        <div className="flex border border-[#cfc4b6]">
          {(
            [
              ['inspector', 'Decision inspector'],
              ['timeline', 'Action timeline'],
            ] as Array<[View, string]>
          ).map(([value, label]) => (
            <button
              key={value}
              type="button"
              onClick={() => setView(value)}
              className={`px-4 py-2 text-xs transition-colors ${
                view === value
                  ? 'bg-[#29231d] text-[#f6f2e9]'
                  : 'bg-[#fffdf8] text-[#4a423b] hover:bg-[#f6f2e9]'
              }`}
            >
              {label}
            </button>
          ))}
        </div>
      </section>

      {view === 'inspector' ? (
        <Inspector hand={hand} frames={frames} step={step} />
      ) : (
        <Timeline hand={hand} frames={frames} step={step} onStep={setStep} />
      )}

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
            className="border border-[#cfc4b6] px-4 py-1.5 text-sm disabled:opacity-35 hover:bg-[#f6f2e9]"
          >
            ◀
          </button>
          <span className="w-24 text-center font-mono text-xs text-[#756a60]">
            {step} / {frames.length - 1}
          </span>
          <button
            type="button"
            onClick={() => move(1)}
            disabled={last}
            className="border border-[#cfc4b6] px-4 py-1.5 text-sm disabled:opacity-35 hover:bg-[#f6f2e9]"
          >
            ▶
          </button>
          <span className="ml-2 text-[10px] uppercase tracking-[.07em] text-[#a89f93]">
            arrow keys work
          </span>
        </div>
        <p className="font-mono text-xs">
          {hand.summary.showdown === 1
            ? 'shown down'
            : `${hand.summary.folded_position === 0 ? hand.players.find((p) => p.position === 0)?.name : hand.players.find((p) => p.position === 1)?.name} folded`}
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
