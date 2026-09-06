import Link from 'next/link';

/*
 * A chevron that sits beside a title. Deliberately quiet: it reads as
 * punctuation until it is hovered, so paging through the roster never competes
 * with the name it sits next to.
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
      className="shrink-0 select-none px-1 font-mono text-2xl leading-none text-[#c5bbac] transition-colors hover:text-[#4a423b] focus-visible:text-[#4a423b]"
    >
      {direction === 'previous' ? '‹' : '›'}
    </Link>
  );
}
