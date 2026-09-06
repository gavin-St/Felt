import Link from 'next/link';

/*
 * A paging control pinned to the outer edge of a header. Deliberately quiet --
 * a grey chevron with no border until it is hovered -- so it frames the page
 * without competing with anything inside it.
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
      className="flex h-12 w-8 shrink-0 select-none items-center justify-center rounded-sm font-mono text-3xl leading-none text-[#c5bbac] transition hover:bg-[#efe8db] hover:text-[#4a423b] focus-visible:bg-[#efe8db] focus-visible:text-[#4a423b]"
    >
      {direction === 'previous' ? '‹' : '›'}
    </Link>
  );
}

/* Holds the gutter when there is nowhere to step, so the header does not
 * shift sideways between pages. */
export function StepSpacer() {
  return <span className="h-12 w-8 shrink-0" aria-hidden />;
}
