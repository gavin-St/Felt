import type { GlyphName } from '@/lib/bots';

/**
 * Hand-authored glyphs rather than an icon set, because colour cannot carry
 * eight identities on its own. Each shape is distinguishable in silhouette, so
 * the bots stay apart in greyscale, for colour-blind viewers, and on video.
 */
const PATHS: Record<GlyphName, React.ReactNode> = {
  // The Cannon — a burst
  burst: (
    <path d="M12 2l2.2 5.4L20 5l-2.4 5.6L23 12l-5.4 2.2L20 19l-5.6-2.4L12 22l-2.2-5.4L4 19l2.4-5.6L1 12l5.4-2.2L4 5l5.6 2.4z" />
  ),
  // The Fool — falling
  descend: <path d="M3 4h18L12 21z" />,
  // The Vault — a padlock
  lock: (
    <>
      <path d="M7 10V7a5 5 0 0 1 10 0v3" fill="none" strokeWidth="2.4" stroke="currentColor" />
      <rect x="4" y="10" width="16" height="11" rx="1.5" />
    </>
  ),
  // The Bruiser — a rising peak
  peak: <path d="M12 3l9 18H3z" />,
  // The Solver — a target
  target: (
    <>
      <circle cx="12" cy="12" r="9.2" fill="none" strokeWidth="2.4" stroke="currentColor" />
      <circle cx="12" cy="12" r="3.6" />
      <path d="M12 0v5M12 19v5M0 12h5M19 12h5" strokeWidth="2.4" stroke="currentColor" />
    </>
  ),
  // The Dice — a die face
  die: (
    <>
      <rect x="2.5" y="2.5" width="19" height="19" rx="3.5" fill="none" strokeWidth="2.4" stroke="currentColor" />
      <circle cx="8" cy="8" r="1.9" />
      <circle cx="16" cy="8" r="1.9" />
      <circle cx="12" cy="12" r="1.9" />
      <circle cx="8" cy="16" r="1.9" />
      <circle cx="16" cy="16" r="1.9" />
    </>
  ),
  // The Anchor — an immovable wall
  wall: (
    <>
      <rect x="2" y="5" width="20" height="4.6" rx="1" />
      <rect x="2" y="14.4" width="20" height="4.6" rx="1" />
    </>
  ),
  // The Mouse — a white flag
  flag: (
    <>
      <path d="M6 2v20" strokeWidth="2.6" stroke="currentColor" />
      <path d="M8 3.5h11l-3 4.2 3 4.2H8z" />
    </>
  ),
  // The Sentry — a shield
  shield: (
    <path d="M12 2.2 20 5v6.2c0 5.1-3.2 8.9-8 10.8-4.8-1.9-8-5.7-8-10.8V5zm0 4.1-4.2 1.5v3.4c0 2.9 1.6 5.3 4.2 6.8 2.6-1.5 4.2-3.9 4.2-6.8V7.8z" />
  ),
  // The Spark — a flame
  flame: (
    <path d="M13.1 1.7c.8 4.6-2.9 5.4-2.1 8.7 1-1.5 2.2-2.3 3.5-3.1 3.2 2.4 5.1 5.1 4.3 8.6-.7 3.4-3.6 5.9-7.1 5.9-4.1 0-7.3-3.1-7.3-7.1 0-4.8 3.8-7.6 8.7-13zM12 12.1c-1.9 2-3.2 3.3-2.8 5 .3 1.3 1.4 2.2 2.8 2.2s2.6-1 2.8-2.4c.3-1.7-.9-3-2.8-4.8z" />
  ),
  // The Switch — a split path
  split: (
    <>
      <path d="M10.2 3h3.6v5.4c0 1.6.7 2.8 2.2 3.7l3.8 2.2-1.8 3.1-3.8-2.2a8.4 8.4 0 0 1-2.2-1.8 8.4 8.4 0 0 1-2.2 1.8L6 17.4l-1.8-3.1L8 12.1c1.5-.9 2.2-2.1 2.2-3.7z" />
      <path d="m16.4 16.1 4.8.1-2.3 4.2zM7.6 16.1l-4.8.1 2.3 4.2z" />
    </>
  ),
  // The Lockpick — a key
  key: (
    <path d="M14.4 3a6.4 6.4 0 0 0-5.9 8.9L1.8 18.6V22h3.5v-2.2h2.3v-2.3h2.3l1.5-1.5A6.4 6.4 0 1 0 14.4 3zm2.8 5.7a2 2 0 1 1 0-4 2 2 0 0 1 0 4z" />
  ),
  // The Probe — a narrow signal finding a boundary
  probe: (
    <>
      <path d="M3 11h12v2H3zM15 7l6 5-6 5z" />
      <circle cx="6" cy="12" r="4" fill="none" strokeWidth="2" stroke="currentColor" />
    </>
  ),
};

export function BotGlyph({
  glyph,
  color,
  size = 20,
  title,
}: {
  glyph: GlyphName;
  color: string;
  size?: number;
  title?: string;
}) {
  return (
    <svg
      viewBox="0 0 24 24"
      width={size}
      height={size}
      fill={color}
      color={color}
      role={title ? 'img' : 'presentation'}
      aria-hidden={title ? undefined : true}
      aria-label={title}
      style={{ flexShrink: 0 }}
    >
      {title ? <title>{title}</title> : null}
      {PATHS[glyph]}
    </svg>
  );
}
