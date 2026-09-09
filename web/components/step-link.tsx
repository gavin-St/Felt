'use client';

import Link from 'next/link';
import { useEffect, useState } from 'react';

/*
 * How many steps the arrows have taken this session. The secret bot is not
 * placed at the end of the roster -- it is behind the whole roster, and this
 * is the count that says whether it has been walked. Per tab, so it resets
 * with the session and cannot be reached by typing a URL into a fresh one.
 */
const STEP_KEY = 'felt:bot-steps';

function stepsTaken(): number {
  try {
    return Number(sessionStorage.getItem(STEP_KEY) ?? '0') || 0;
  } catch {
    return 0;
  }
}

/*
 * A paging chevron pinned to the outer edge of a header. It is absolutely
 * positioned and invisible until the header is hovered, so it costs the layout
 * nothing: the title keeps the full width and does not move when the arrows
 * appear. Narrow, faint with the header, and close to solid only under the
 * pointer.
 *
 * The parent needs `group relative`. Pass `offset` to slide the pair up or
 * down past whatever sits in that corner -- the two pages put different things
 * at the edges.
 */
export function StepLink({
  href,
  direction,
  label,
  offset = 'top-1/2',
  track = false,
  unlockHref,
  unlockAfter,
}: {
  href: string;
  direction: 'previous' | 'next';
  label: string;
  offset?: string;
  /** Count this press toward walking the roster. */
  track?: boolean;
  /** Where this arrow leads once the roster has been walked. */
  unlockHref?: string;
  unlockAfter?: number;
}) {
  const [target, setTarget] = useState(href);

  useEffect(() => {
    setTarget(
      unlockHref !== undefined &&
        unlockAfter !== undefined &&
        stepsTaken() >= unlockAfter
        ? unlockHref
        : href,
    );
  }, [href, unlockHref, unlockAfter]);

  const count = () => {
    if (!track) return;
    try {
      sessionStorage.setItem(STEP_KEY, String(stepsTaken() + 1));
    } catch {
      /* A browser that refuses storage simply never finds it. */
    }
  };

  return (
    <Link
      href={target}
      onClick={count}
      rel={direction === 'next' ? 'next' : 'prev'}
      title={label}
      aria-label={label}
      className={`absolute z-10 -translate-y-1/2 ${offset} ${
        direction === 'previous' ? 'left-0' : 'right-0'
      } flex h-12 w-6 select-none items-center justify-center rounded-sm bg-[#231f1b] font-mono text-xl leading-none text-[#f6f2e9] opacity-0 transition-opacity hover:opacity-85 focus-visible:opacity-85 group-hover:opacity-35`}
    >
      {direction === 'previous' ? '‹' : '›'}
    </Link>
  );
}
