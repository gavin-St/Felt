import { Link } from 'react-router';

export function NotFound({ what = 'page' }: { what?: string }) {
  return (
    <main className="min-h-screen bg-[#f6f2e9] text-[#241f1b]">
      <div className="mx-auto max-w-[680px] px-6 py-20">
        <h1 className="font-serif text-3xl">No such {what}</h1>
        <p className="mt-4 leading-relaxed text-[#4a423a]">
          Nothing on this site links here, so the address was probably typed or
          has gone stale since it was written down.
        </p>
        <Link
          to="/"
          className="mt-8 inline-block font-mono text-xs text-[#756b60] underline"
        >
          &larr; Matchup matrix
        </Link>
      </div>
    </main>
  );
}
