import { Link } from 'react-router';
import { useMemo, useState } from 'react';

import { signed } from '@/lib/dashboard';

export type HandRow = {
  bucket: string;
  hands: number;
  adjustedBb: number;
  adjustedBbPerHand: number;
};

type Column = { key: keyof HandRow; label: string; numeric: boolean };

const COLUMNS: Column[] = [
  { key: 'bucket', label: 'Hand', numeric: false },
  { key: 'hands', label: 'Hands', numeric: true },
  { key: 'adjustedBb', label: 'Total BB', numeric: true },
  { key: 'adjustedBbPerHand', label: 'BB / hand', numeric: true },
];

/*
 * All 169 starting-hand buckets, searchable and sortable. A header click sorts
 * descending first, then ascending, because the question is almost always
 * "what made the most" rather than "what made the least".
 */
/*
 * `replayBase` is a query string rather than a callback because this is a
 * client component and a server page cannot hand it a function. When it is
 * present every bucket becomes a link into the replay browser already
 * narrowed to that hand; when it is absent -- a production build, where the
 * replay does not exist -- the cell is plain text and nothing dangles.
 */
export function HandTable({
  rows,
  replayBase,
}: {
  rows: HandRow[];
  replayBase?: string;
}) {
  const [query, setQuery] = useState('');
  const [sortKey, setSortKey] = useState<keyof HandRow>('adjustedBbPerHand');
  const [descending, setDescending] = useState(true);

  const visible = useMemo(() => {
    const needle = query.trim().toLowerCase();
    const filtered = needle
      ? rows.filter((row) => row.bucket.toLowerCase().includes(needle))
      : rows;
    const direction = descending ? -1 : 1;
    return [...filtered].sort((left, right) => {
      const a = left[sortKey];
      const b = right[sortKey];
      if (typeof a === 'string' || typeof b === 'string') {
        return direction * String(a).localeCompare(String(b));
      }
      return direction * (a - b);
    });
  }, [rows, query, sortKey, descending]);

  const click = (key: keyof HandRow) => {
    if (key === sortKey) {
      setDescending((current) => !current);
      return;
    }
    setSortKey(key);
    setDescending(true);
  };

  return (
    <div>
      <div className="mb-3 flex items-baseline gap-3">
        <input
          value={query}
          onChange={(event) => setQuery(event.target.value)}
          placeholder="Search a hand — KK, AKs, 72o"
          aria-label="Search starting hands"
          className="w-56 border border-[#cfc4b6] bg-[#fffdf8] px-3 py-2 font-mono text-sm outline-none focus:border-[#8b8177]"
        />
        <span className="font-mono text-[11px] text-[#8b8177]">
          {visible.length} of {rows.length} hands
          {replayBase ? ' · click a hand to replay it' : ''}
        </span>
      </div>

      <div className="max-h-[26rem] overflow-y-auto border border-[#cfc4b6]">
        <table className="w-full border-collapse bg-[#fffdf8] text-sm">
          <thead className="sticky top-0 z-10 bg-[#f3ede2]">
            <tr>
              {COLUMNS.map((column) => (
                <th
                  key={column.key}
                  className={`border-b border-[#cfc4b6] p-3 font-semibold ${column.numeric ? 'text-right' : 'text-left'}`}
                >
                  <button
                    type="button"
                    onClick={() => click(column.key)}
                    className="cursor-pointer underline-offset-2 hover:underline"
                  >
                    {column.label}
                    {sortKey === column.key ? (
                      <span className="ml-1 font-mono text-[10px] text-[#8b8177]">
                        {descending ? '▼' : '▲'}
                      </span>
                    ) : null}
                  </button>
                </th>
              ))}
            </tr>
          </thead>
          <tbody>
            {visible.map((row) => (
              <tr key={row.bucket}>
                <td className="border-b border-[#e3dbd0] p-3 font-mono">
                  {replayBase ? (
                    <Link
                      to={`${replayBase}&hand=${encodeURIComponent(row.bucket)}`}
                      title={`Replay ${row.bucket} hands`}
                      className="underline decoration-[#cfc4b6] underline-offset-2 hover:decoration-[#241f1b]"
                    >
                      {row.bucket}
                    </Link>
                  ) : (
                    row.bucket
                  )}
                </td>
                <td className="border-b border-[#e3dbd0] p-3 text-right">
                  {row.hands.toLocaleString()}
                </td>
                <td
                  className={`border-b border-[#e3dbd0] p-3 text-right font-mono ${row.adjustedBb >= 0 ? 'text-[#087343]' : 'text-[#b52d24]'}`}
                >
                  {signed(row.adjustedBb, 1)}
                </td>
                <td className="border-b border-[#e3dbd0] p-3 text-right font-mono">
                  {signed(row.adjustedBbPerHand)}
                </td>
              </tr>
            ))}
            {visible.length === 0 ? (
              <tr>
                <td colSpan={4} className="p-6 text-center text-[#8b8177]">
                  No hand matches “{query}”.
                </td>
              </tr>
            ) : null}
          </tbody>
        </table>
      </div>
    </div>
  );
}
