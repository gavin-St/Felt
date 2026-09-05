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
