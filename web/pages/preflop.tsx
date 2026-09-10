import { PreflopCharts } from '@/components/preflop-charts';
import { useTitle } from '@/lib/title';

export default function PreflopPage() {
  useTitle(
    'Preflop Charts — Felt',
    'The built-in heads-up preflop ranges used by Felt poker bots.',
  );
  return <PreflopCharts />;
}
