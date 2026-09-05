export type PreflopAction = 'fold' | 'passive' | 'value' | 'bluff' | 'all-in';

export type PreflopCell = {
  hand: string;
  action: PreflopAction;
};

export type PreflopChart = {
  id: string;
  context: string;
  title: string;
  description: string;
  passiveLabel: 'Limp' | 'Check' | 'Call';
  cells: PreflopCell[];
};

const ranks = ['A', 'K', 'Q', 'J', 'T', '9', '8', '7', '6', '5', '4', '3', '2'];

const smallBlindFirstIn = [
  'CVVVVCCCCCCCC',
  'CCVVCCCCCCCCC',
  'VVVBCCCCCCCCC',
  'VVCVCCCCCCBBB',
  'VCCCVCCCCBBFF',
  'CCCCCVCCCBBFF',
  'CCCCCCVCCBBFF',
  'CCCCCCCVCCBFF',
  'CCCBBBBCCCCBF',
  'CCBFFFFFCCCBF',
  'CCBFFFFFFFCBF',
  'CBBFFFFFFFFCC',
  'CBBFFFFFFFFFC',
] as const;

const bigBlindVsSmallRaise = [
  'VVVVVCCCCCCCC',
  'VVVVVCCCCCCCC',
  'VVVVVCCCCCCCC',
  'VCCVVBCCCCCCC',
  'CCCCVBBCCCCCC',
  'CCCCCVBCCCCCC',
  'CCCCCCCBCCCCC',
  'CCCCCCCCBCCCC',
  'CCCCCCCBCBCCC',
  'CCCBBFFBBCBCC',
  'CCBFFFFFBBCCC',
  'CBBFFFFFFFFCC',
  'BBBFFFFFFFFFC',
] as const;

const codeToAction: Record<string, PreflopAction> = {
  F: 'fold',
  C: 'passive',
  V: 'value',
  B: 'bluff',
};

function handAt(row: number, column: number) {
  if (row === column) return `${ranks[row]}${ranks[column]}`;
  if (row < column) return `${ranks[row]}${ranks[column]}s`;
  return `${ranks[column]}${ranks[row]}o`;
}

function cellsFromMatrix(matrix: readonly string[]): PreflopCell[] {
  return matrix.flatMap((line, row) =>
    line.split('').map((code, column) => ({
      hand: handAt(row, column),
      action: codeToAction[code],
    })),
  );
}

function cellsFromAction(
  actionFor: (hand: string) => PreflopAction,
): PreflopCell[] {
  return ranks.flatMap((_, row) =>
    ranks.map((__, column) => {
      const hand = handAt(row, column);
      return { hand, action: actionFor(hand) };
    }),
  );
}

const firstInByHand = new Map(
  cellsFromMatrix(smallBlindFirstIn).map((cell) => [cell.hand, cell.action]),
);
const bbSmallByHand = new Map(
  cellsFromMatrix(bigBlindVsSmallRaise).map((cell) => [cell.hand, cell.action]),
);

const sbSmallValue = new Set(['AA', 'AKo', 'KK']);
const sbSmallBluff = new Set(['Q7o', 'K6o', 'K5o', 'A3o', 'A2o']);
const sbSmallFold = new Set(['K4o', 'Q6o', 'J7o', 'T7o', '65o']);

const mediumValue = new Set([
  'AKs',
  'AQs',
  'AJs',
  'AQo',
  'QQ',
  'JJ',
  'AA',
  'KK',
  'AKo',
]);
const mediumBluff = new Set(['J4s', 'Q5o', 'Q4o', 'K3o', 'K2o']);
const mediumCall = new Set([
  'ATs',
  'KQs',
  'KJs',
  'QJs',
  'KQo',
  'AJo',
  'KJo',
  'ATo',
  'TT',
  '99',
  '88',
  '95s',
  '85s',
  '74s',
  '43s',
]);

const largeShove = new Set(['AA', 'KK', 'QQ', 'AKs', 'AKo']);
const largeCall = new Set(['JJ', 'TT', 'AQs', 'AJs', 'KQs']);

const sbVsSmallRaise = cellsFromAction((hand) => {
  if (sbSmallValue.has(hand)) return 'value';
  if (sbSmallBluff.has(hand)) return 'bluff';
  if (sbSmallFold.has(hand)) return 'fold';
  const firstIn = firstInByHand.get(hand) ?? 'fold';
  if (firstIn === 'value') return 'value';
  if (firstIn === 'bluff' || firstIn === 'passive') return 'passive';
  return 'fold';
});

export const preflopCharts: PreflopChart[] = [
  {
    id: 'sb-first-in',
    context: 'NO VOLUNTARY ACTION',
    title: 'Small blind first in',
    description: 'Open to 2.5 bb, complete the blind, or fold.',
    passiveLabel: 'Limp',
    cells: cellsFromMatrix(smallBlindFirstIn),
  },
  {
    id: 'bb-vs-limp',
    context: 'NO RAISE',
    title: 'Big blind versus limp',
    description: 'Raise the BB small-raise range to 4 bb; check the rest.',
    passiveLabel: 'Check',
    cells: cellsFromAction((hand) => {
      const action = bbSmallByHand.get(hand) ?? 'fold';
      return action === 'value' || action === 'bluff' ? action : 'passive';
    }),
  },
  {
    id: 'bb-vs-small',
    context: 'FACING < 10 BB',
    title: 'Big blind versus small raise',
    description: 'The supplied BB-versus-SB opening response.',
    passiveLabel: 'Call',
    cells: cellsFromMatrix(bigBlindVsSmallRaise),
  },
  {
    id: 'sb-vs-small',
    context: 'FACING < 10 BB',
    title: 'Small blind versus small raise',
    description: 'The limp-response chart, completed for every starting hand.',
    passiveLabel: 'Call',
    cells: sbVsSmallRaise,
  },
  {
    id: 'vs-medium',
    context: 'FACING 10–<40 BB',
    title: 'Either seat versus medium raise',
    description: 'Based on the supplied SB response to a standard 3-bet.',
    passiveLabel: 'Call',
    cells: cellsFromAction((hand) => {
      if (mediumValue.has(hand)) return 'value';
      if (mediumBluff.has(hand)) return 'bluff';
      if (mediumCall.has(hand)) return 'passive';
      return 'fold';
    }),
  },
  {
    id: 'vs-large',
    context: 'FACING 40–<75 BB',
    title: 'Either seat versus large raise',
    description:
      'A conservative continuation range for heavily committed pots.',
    passiveLabel: 'Call',
    cells: cellsFromAction((hand) => {
      if (largeShove.has(hand)) return 'all-in';
      if (largeCall.has(hand)) return 'passive';
      return 'fold';
    }),
  },
  {
    id: 'vs-all-in-sized',
    context: 'FACING ≥ 75 BB',
    title: 'Either seat versus all-in-sized raise',
    description: 'Continue only with QQ+ and AK.',
    passiveLabel: 'Call',
    cells: cellsFromAction((hand) =>
      largeShove.has(hand) ? 'passive' : 'fold',
    ),
  },
];

export const preflopActionStyles: Record<PreflopAction, string> = {
  fold: 'border-[#d8cfc2] bg-[#f4efe7] text-[#8b8176]',
  passive: 'border-[#26733f] bg-[#32844b] text-white',
  value: 'border-[#a72620] bg-[#c83b31] text-white',
  bluff: 'border-[#3152a1] bg-[#4166ba] text-white',
  'all-in': 'border-[#29231d] bg-[#29231d] text-white',
};
