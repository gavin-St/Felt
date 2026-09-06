import Link from 'next/link';

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
}: {
  href: string;
  direction: 'previous' | 'next';
  label: string;
  offset?: string;
}) {
  return (
    <Link
      href={href}
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
