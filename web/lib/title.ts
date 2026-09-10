import { useEffect } from 'react';

const DEFAULT_TITLE = 'Felt — Bot Matchups';
const DEFAULT_DESCRIPTION = 'Head-to-head results for Felt poker bots.';

/*
 * What the Metadata API did, for the two things this site sets. A single page
 * app has one document, so the title is a side effect of arriving somewhere
 * rather than a property of a file, and it has to be put back on the way out
 * or the last page visited names the tab forever.
 */
export function useTitle(title?: string, description?: string) {
  useEffect(() => {
    document.title = title ?? DEFAULT_TITLE;
    const tag = document.querySelector('meta[name="description"]');
    if (tag) tag.setAttribute('content', description ?? DEFAULT_DESCRIPTION);
    return () => {
      document.title = DEFAULT_TITLE;
      if (tag) tag.setAttribute('content', DEFAULT_DESCRIPTION);
    };
  }, [title, description]);
}
