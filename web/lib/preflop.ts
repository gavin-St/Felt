export type PreflopAction = 'fold' | 'passive' | 'value' | 'bluff' | 'all-in';

export type PreflopCell = {
  hand: string;
  action: PreflopAction;
};

export type PreflopChart = {
  id: string;
  context: string;
  title: string;
  passiveLabel: 'Limp' | 'Check' | 'Call';
  cells: PreflopCell[];
};

const ranks = ['A', 'K', 'Q', 'J', 'T', '9', '8', '7', '6', '5', '4', '3', '2'];

const smallBlindFirstIn = [
  'CVVVVVVVVVVVV',
  'CCVVVVCCCCCCC',
  'VVVVVVCCCCCCC',
  'VVCVCCCCCCBBB',
  'VVCCVCCCCBBFF',
  'CCCCCVCCCBBFF',
  'CCCCCCVCCBBFF',
  'CCCCCCCVCCBFF',
  'BCCBBBBCCCCBF',
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

const bbVsLimpValuePairs = new Set([
  'AA',
  'KK',
  'QQ',
  'JJ',
  'TT',
  '99',
  '88',
  '77',
]);
const bbVsLimpAddedBluffs = new Set([
  'A5s',
  'A4s',
  'Q2s',
  'T2s',
  '32s',
  '43s',
  '65s',
]);

const bbVsSmallAddedBluffs = new Set([
  'A5s',
  'A4s',
  'A2s',
  'K3s',
  '75s',
  '64s',
]);
const bbVsSmallRemovedBluffs = new Set([
  'A2o',
  'K2o',
  'Q2o',
  'K3o',
  'Q3o',
  'Q4o',
  'J5o',
  'T5o',
  '75o',
  '64o',
]);

const sbSmallValue = new Set(['AA', 'AKo', 'KK']);
const sbSmallBluff = new Set([
  'K6o',
  'A3o',
  'A2o',
  'T9s',
  '76s',
  '65s',
  '54s',
  'T5s',
  '87s',
]);
const sbSmallCall = new Set(['AJo', 'ATo', 'KJo', 'QTo', 'KQo']);
const sbSmallFold = new Set([
  'Q7o',
  'K5o',
  'K4o',
  'Q6o',
  'J7o',
  'T7o',
  '65o',
  'K3o',
  'K2o',
  'Q5o',
  'Q4o',
  'Q3o',
  'Q2o',
  'J6o',
  'T6o',
  '96o',
  '86o',
]);

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
  'TT',
]);
const mediumBluff = new Set(['87s', 'KTs', 'K9s', 'A5s', 'A4s']);
const mediumCall = new Set([
  '55',
  '44',
  '33',
  '22',
  'A9s',
  'A8s',
  'A7s',
  'A6s',
  'A3s',
  'A2s',
  'Q9s',
  'J9s',
  'T9s',
  '98s',
  'QJo',
  'ATs',
  'KQs',
  'KJs',
  'QJs',
  'QTs',
  'JTs',
  'KQo',
  'AJo',
  'KJo',
  'ATo',
  '99',
  '88',
  '77',
  '66',
  '76s',
  '65s',
  '54s',
]);

const largeShove = new Set(['AA', 'KK', 'QQ', 'AKs', 'AKo']);
const largeCall = new Set([
  'JJ',
  'TT',
  'AQs',
  'AJs',
  'KQs',
  '65s',
  '76s',
  '87s',
  'T9s',
  'AQo',
  'AJo',
]);

const sbVsSmallRaise = cellsFromAction((hand) => {
  if (sbSmallValue.has(hand)) return 'value';
  if (sbSmallBluff.has(hand)) return 'bluff';
  if (sbSmallCall.has(hand)) return 'passive';
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
    passiveLabel: 'Limp',
    cells: cellsFromMatrix(smallBlindFirstIn),
  },
  {
    id: 'bb-vs-limp',
    context: 'NO RAISE',
    title: 'Big blind versus limp',
    passiveLabel: 'Check',
    cells: cellsFromAction((hand) => {
      const action = bbSmallByHand.get(hand) ?? 'fold';
      if (bbVsLimpValuePairs.has(hand)) return 'value';
      if (bbVsLimpAddedBluffs.has(hand)) return 'bluff';
      if (action === 'value') return 'value';
      if (action === 'bluff' && hand.endsWith('s')) return 'bluff';
      return 'passive';
    }),
  },
  {
    id: 'bb-vs-small',
    context: 'TO CALL < 6 BB',
    title: 'Big blind versus small raise',
    passiveLabel: 'Call',
    cells: cellsFromAction((hand) => {
      if (bbVsSmallAddedBluffs.has(hand)) return 'bluff';
      if (bbVsSmallRemovedBluffs.has(hand)) return 'passive';
      return bbSmallByHand.get(hand) ?? 'fold';
    }),
  },
  {
    id: 'sb-vs-small',
    context: 'TO CALL < 6 BB',
    title: 'Small blind versus small raise',
    passiveLabel: 'Call',
    cells: sbVsSmallRaise,
  },
  {
    id: 'vs-medium',
    context: 'TO CALL 6–<22 BB',
    title: 'Either seat versus medium raise',
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
    context: 'TO CALL 22–<45 BB',
    title: 'Either seat versus large raise',
    passiveLabel: 'Call',
    cells: cellsFromAction((hand) => {
      if (largeShove.has(hand)) return 'all-in';
      if (largeCall.has(hand)) return 'passive';
      return 'fold';
    }),
  },
  {
    id: 'vs-all-in-sized',
    context: 'TO CALL ≥ 45 BB',
    title: 'Either seat versus all-in-sized raise',
    passiveLabel: 'Call',
    cells: cellsFromAction((hand) =>
      largeShove.has(hand) ? 'passive' : 'fold',
    ),
  },
];

export const preflopActionStyles: Record<
  PreflopAction,
  { borderColor: string; backgroundColor: string; color: string }
> = {
  fold: {
    borderColor: '#d8cfc2',
    backgroundColor: '#f4efe7',
    color: '#8b8176',
  },
  passive: {
    borderColor: '#26733f',
    backgroundColor: '#32844b',
    color: '#ffffff',
  },
  value: {
    borderColor: '#a72620',
    backgroundColor: '#c83b31',
    color: '#ffffff',
  },
  bluff: {
    borderColor: '#c94635',
    backgroundColor: '#e05b47',
    color: '#ffffff',
  },
  'all-in': {
    borderColor: '#711018',
    backgroundColor: '#8f111b',
    color: '#ffffff',
  },
};
