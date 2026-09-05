import type { Metadata } from 'next';

import { PreflopCharts } from '@/components/preflop-charts';

export const metadata: Metadata = {
  title: 'Preflop Charts — Felt',
  description: 'The built-in heads-up preflop ranges used by Felt poker bots.',
};

export default function PreflopPage() {
  return <PreflopCharts />;
}
