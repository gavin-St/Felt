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

/*
 * One 13x13 grid per spot, transcribed from the ranges in
 * harness/src/preflop_chart.cpp. Rows and columns run A down to 2; above the
 * diagonal is suited and below it offsuit. F fold, C call or limp, V value
 * raise, B bluff raise, A all-in.
 */
const matrices: Record<string, readonly string[]> = {
  'sb-first-in': [
    'CVVVVVVVVVVVV', 'CCVVVVCCCCCCC', 'VVVVVVCCCCCCC',
    'VVCVVCCCCCBBB', 'VVCCVCCCCCBBB', 'CCCCCVCCCBBBB',
    'CCCCCCVCCBBFB', 'CCCCCCCVCCBFF', 'BCCBBBBCCCCBF',
    'CCBFFFFFCCCBF', 'CCBFFFFFFFCBF', 'CBBFFFFFFFFCC',
    'CBBFFFFFFFFFC',
  ],
  'bb-vs-limp': [
    'VVVVVCCCCBBCC', 'VVVVVCCCCCCCC', 'VVVVVCCCCCCCB',
    'VCCVVBCCCCCCC', 'CCCCVBBCCCCCB', 'CCCCCVBCCCCCC',
    'CCCCCCVBCCCCC', 'CCCCCCCVBCCCC', 'CCCCCCCCCBCCC',
    'CCCCCCCCCCBCC', 'CCCCCCCCCCCBC', 'CCCCCCCCCCCCB',
    'CCCCCCCCCCCCC',
  ],
  'bb-vs-small': [
    'VVVVVCCCCBBCB', 'VVVVVCCCCCCBC', 'VVVVVCCCCCCCC',
    'VCCVVBCCCCCCC', 'CCCCVBBCCCCCC', 'CCCCCVBCCCCCC',
    'CCCCCCCBCCCCC', 'CCCCCCCCBBCCC', 'CCCCCCCBCBBCC',
    'CCCCCFFCBCBCC', 'CCCFFFFFCBCCC', 'CCCFFFFFFFFCC',
    'CCCFFFFFFFFFC',
  ],
  'sb-vs-small': [
    'VVVVVVCCCCCCC', 'VVVVVVCCCCCCC', 'VCVVVVCCCCCCC',
    'CCCVVCCCCCCCC', 'CVCCVBCCCBCCC', 'CCCCCVCCCCCCC',
    'CCCCCCVBCCCFC', 'CCFFFCCVBCCFF', 'CBFFFFFCCBCCF',
    'CFFFFFFFFCBCF', 'CFFFFFFFFFCCF', 'BFFFFFFFFFFCC',
    'BFFFFFFFFFFFC',
  ],
  'vs-three-bet': [
    'VVVVCCCCCCCCC', 'VVCBCCCCCCCCC', 'VCVCCCCCCCCBB',
    'CCCVCCCCFFFFF', 'CCCCVCCFFFFFF', 'CFFCCCCCFFFFF',
    'CFFFFFCBCFFFF', 'FFFFFFFCBFFFF', 'FFFFFFFFCBFFF',
    'FFFFFFFFFCCFF', 'FFFFFFFFFFCCC', 'FFFFFFFFFFFCF',
    'FFFFFFFFFFFFC',
  ],
  'vs-four-bet': [
    'VVCCCCFFFBBFF', 'VVCBBBFFBFFFF', 'BFVCCFFFFFFFF',
    'FFFVCFFFFFFFF', 'FFFFCCFFFFFFF', 'FFFFFCFFFFFFF',
    'FFFFFFCCFFFFF', 'FFFFFFFCCFFFF', 'FFFFFFFFCCFFF',
    'FFFFFFFFFCCFF', 'FFFFFFFFFFFFF', 'FFFFFFFFFFFFF',
    'FFFFFFFFFFFFF',
  ],
  'vs-five-bet': [
    'AACCFFFFFFFFF', 'AACFFFFFFFFFF', 'CFAFFFFFFFFFF',
    'CFFCFFFFFFFFF', 'FFFFCCFFFFFFF', 'FFFFFFCFFFFFF',
    'FFFFFFFCFFFFF', 'FFFFFFFFCFFFF', 'FFFFFFFFFCFFF',
    'FFFFFFFFFFFFF', 'FFFFFFFFFFFFF', 'FFFFFFFFFFFFF',
    'FFFFFFFFFFFFF',
  ],
  'vs-all-in-sized': [
    'AAFFFFFFFFFFF', 'AAFFFFFFFFFFF', 'FFAFFFFFFFFFF',
    'FFFFFFFFFFFFF', 'FFFFFFFFFFFFF', 'FFFFFFFFFFFFF',
    'FFFFFFFFFFFFF', 'FFFFFFFFFFFFF', 'FFFFFFFFFFFFF',
    'FFFFFFFFFFFFF', 'FFFFFFFFFFFFF', 'FFFFFFFFFFFFF',
    'FFFFFFFFFFFFF',
  ],
};

const codeToAction: Record<string, PreflopAction> = {
  F: 'fold',
  C: 'passive',
  V: 'value',
  B: 'bluff',
  A: 'all-in',
};

function handAt(row: number, column: number) {
  if (row === column) return `${ranks[row]}${ranks[column]}`;
  if (row < column) return `${ranks[row]}${ranks[column]}s`;
  return `${ranks[column]}${ranks[row]}o`;
}

function cellsFor(id: string): PreflopCell[] {
  return matrices[id].flatMap((line, row) =>
    line.split('').map((code, column) => ({
      hand: handAt(row, column),
      action: codeToAction[code],
    })),
  );
}

/*
 * The order is the order the money arrives in: unopened, then limped, then
 * each facing-raise band by the amount still owed. The bands come from
 * recognize_size_spot(); the sizes below are what makes a raise land in the
 * next one along.
 */
export const preflopCharts: PreflopChart[] = [
  {
    id: 'sb-first-in',
    context: 'NO VOLUNTARY ACTION',
    title: 'Small blind first in',
    passiveLabel: 'Limp',
    cells: cellsFor('sb-first-in'),
  },
  {
    id: 'bb-vs-limp',
    context: 'NO RAISE',
    title: 'Big blind versus limp',
    passiveLabel: 'Check',
    cells: cellsFor('bb-vs-limp'),
  },
  {
    id: 'bb-vs-small',
    context: 'TO CALL < 6 BB',
    title: 'Big blind versus small raise',
    passiveLabel: 'Call',
    cells: cellsFor('bb-vs-small'),
  },
  {
    id: 'sb-vs-small',
    context: 'TO CALL < 6 BB',
    title: 'Small blind versus small raise',
    passiveLabel: 'Call',
    cells: cellsFor('sb-vs-small'),
  },
  {
    id: 'vs-three-bet',
    context: 'TO CALL 6–<16 BB',
    title: 'Either seat versus a three-bet',
    passiveLabel: 'Call',
    cells: cellsFor('vs-three-bet'),
  },
  {
    id: 'vs-four-bet',
    context: 'TO CALL 16–<31 BB',
    title: 'Either seat versus a four-bet',
    passiveLabel: 'Call',
    cells: cellsFor('vs-four-bet'),
  },
  {
    id: 'vs-five-bet',
    context: 'TO CALL 31–<50 BB',
    title: 'Either seat versus a five-bet',
    passiveLabel: 'Call',
    cells: cellsFor('vs-five-bet'),
  },
  {
    id: 'vs-all-in-sized',
    context: 'TO CALL ≥ 50 BB',
    title: 'Either seat versus an all-in-sized raise',
    passiveLabel: 'Call',
    cells: cellsFor('vs-all-in-sized'),
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
