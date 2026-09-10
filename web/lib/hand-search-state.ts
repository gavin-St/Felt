import { DEFAULT_HAND_SORT, HAND_FILTERS, HAND_SORTS, consistentFilters, type HandFilter } from './hands';

export const SAVED_HAND_SEARCH_KEY = 'felt.hands.search';
export type HandSearchState = {
  bot?: number;
  opponent?: number;
  hand: string;
  filters: HandFilter[];
  sort: string;
  offset: number;
  from?: string;
};

export function restoreHandSearch(query: URLSearchParams, raw: string | null): HandSearchState {
  let saved: Record<string, unknown> = {};
  try {
    const parsed: unknown = JSON.parse(raw ?? '{}');
    if (parsed && typeof parsed === 'object' && !Array.isArray(parsed)) saved = parsed as Record<string, unknown>;
  } catch { /* Ignore unavailable or corrupt storage. */ }
  // A URL emitted by the form is a complete snapshot, including cleared fields.
  // Links from bot/matchup pages are partial and may inherit remembered fields.
  const complete = query.get('search') === '1';
  const field = (name: string) => query.has(name) ? query.get(name) : complete ? undefined : saved[name];
  const id = (value: unknown) => {
    const number = Number(value);
    return Number.isSafeInteger(number) && number > 0 ? number : undefined;
  };
  const text = (value: unknown) => typeof value === 'string' ? value : '';
  const rawFilters = field('filters');
  const filters = Array.isArray(rawFilters) ? rawFilters : text(rawFilters).split(',');
  const known = new Set(HAND_FILTERS.map(([name]) => name));
  const sort = text(field('sort'));
  const from = text(field('from'));
  return {
    bot: id(field('bot')),
    opponent: id(field('opponent')),
    hand: text(field('hand')),
    filters: consistentFilters(filters.filter((item): item is HandFilter => typeof item === 'string' && known.has(item as HandFilter))),
    sort: HAND_SORTS.some(([name]) => name === sort) ? sort : DEFAULT_HAND_SORT,
    offset: id(field('offset')) ?? 0,
    from: from.startsWith('/') && !from.startsWith('//') ? from : undefined,
  };
}

export function handSearchUrl(state: HandSearchState): string {
  const query = new URLSearchParams({ search: '1', sort: state.sort });
  if (state.bot) query.set('bot', String(state.bot));
  if (state.opponent) query.set('opponent', String(state.opponent));
  if (state.hand.trim()) query.set('hand', state.hand.trim());
  if (state.filters.length) query.set('filters', state.filters.join(','));
  if (state.offset) query.set('offset', String(state.offset));
  if (state.from) query.set('from', state.from);
  return `/hands?${query}`;
}
