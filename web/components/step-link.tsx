import Link from 'next/link';

/*
 * A paging chevron pinned to the outer edge of a header. It is absolutely
 * positioned and invisible until the header is hovered, so it costs the layout
 * nothing: the title keeps the full width and does not move when the arrows
 * appear. The parent needs `group relative`.
 */
export function StepLink({
  href,
  direction,
  label,
}: {
  href: string;
  direction: 'previous' | 'next';
  label: string;
}) {
  return (
    <Link
      href={href}
      rel={direction === 'next' ? 'next' : 'prev'}
      title={label}
      aria-label={label}
      className={`absolute top-1/2 z-10 -translate-y-1/2 ${
        direction === 'previous' ? 'left-0' : 'right-0'
      } flex h-12 w-8 select-none items-center justify-center rounded-sm font-mono text-3xl leading-none text-[#b3a897] opacity-0 transition-opacity hover:text-[#4a423b] focus-visible:opacity-100 group-hover:opacity-100`}
    >
      {direction === 'previous' ? '‹' : '›'}
    </Link>
  );
}
