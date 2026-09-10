import { HandSearch } from '@/components/hand-search';
import { useTitle } from '@/lib/title';

export default function HandsPage() {
  useTitle('Hand search — Felt');
  return (
    <main className="min-h-screen bg-[#f6f2e9] text-[#241f1b]">
      <div className="mx-auto max-w-[1180px] px-6 py-8 pb-20">
        <HandSearch />
      </div>
    </main>
  );
}
