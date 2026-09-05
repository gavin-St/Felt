import snapshot from '@/data/dashboard.json';

export type Rating = (typeof snapshot.ratings)[number];
export type MatrixResult = (typeof snapshot.matrix)[number];
export type MatchDetail = (typeof snapshot.matches)[number];

export const dashboard = snapshot;

export function signed(value: number, digits = 2) {
  return `${value > 0 ? '+' : ''}${value.toFixed(digits)}`;
}

export function matrixResult(botId: number, opponentId: number) {
  return dashboard.matrix.find(
    (result) => result.bot_id === botId && result.opponent_bot_id === opponentId,
  );
}

const ELO_PER_LOGIT = 400 / Math.log(10);
const BASE_WIN_LOGIT = 1;
const MARGIN_BONUS_LOGIT = 0.15;
const OUTCOME_STANDARD_ERROR_ELO = 100;

function solveLinear(matrix: number[][], values: number[]) {
  const size = matrix.length;
  const augmented = matrix.map((row, index) => [...row, values[index]]);

  for (let column = 0; column < size; column += 1) {
    let pivot = column;
    for (let row = column + 1; row < size; row += 1) {
      if (Math.abs(augmented[row][column]) > Math.abs(augmented[pivot][column])) {
        pivot = row;
      }
    }
    if (Math.abs(augmented[pivot][column]) < 1e-12) {
      throw new Error('Rating graph produced a singular system');
    }
    [augmented[column], augmented[pivot]] = [augmented[pivot], augmented[column]];
    const divisor = augmented[column][column];
    augmented[column] = augmented[column].map((value) => value / divisor);

    for (let row = 0; row < size; row += 1) {
      if (row === column) continue;
      const multiplier = augmented[row][column];
      if (multiplier === 0) continue;
      augmented[row] = augmented[row].map(
        (value, index) => value - multiplier * augmented[column][index],
      );
    }
  }

  return augmented.map((row) => row[size]);
}

export function subsetRatings(botIds: Set<number>) {
  const observations = new Map<number, MatrixResult>();
  for (const result of dashboard.matrix) {
    if (
      botIds.has(result.bot_id) &&
      botIds.has(result.opponent_bot_id) &&
      !observations.has(result.match_id)
    ) {
      observations.set(result.match_id, result);
    }
  }

  const neighbors = new Map<number, Set<number>>();
  for (const botId of botIds) neighbors.set(botId, new Set());
  for (const observation of observations.values()) {
    neighbors.get(observation.bot_id)?.add(observation.opponent_bot_id);
    neighbors.get(observation.opponent_bot_id)?.add(observation.bot_id);
  }

  const ratings = new Map<number, number>();
  const unseen = new Set(botIds);
  while (unseen.size > 0) {
    const start = Math.min(...unseen);
    const pending = [start];
    const component = new Set<number>();
    while (pending.length > 0) {
      const botId = pending.pop()!;
      if (component.has(botId)) continue;
      component.add(botId);
      unseen.delete(botId);
      for (const neighbor of neighbors.get(botId) ?? []) {
        if (!component.has(neighbor)) pending.push(neighbor);
      }
    }

    const ordered = [...component].sort((left, right) => left - right);
    const indexByBot = new Map(ordered.map((botId, index) => [botId, index]));
    const size = ordered.length;
    const normal = Array.from({ length: size + 1 }, () =>
      Array.from({ length: size + 1 }, () => 0),
    );
    const right = Array.from({ length: size + 1 }, () => 0);

    for (const observation of observations.values()) {
      const left = indexByBot.get(observation.bot_id);
      const rightIndex = indexByBot.get(observation.opponent_bot_id);
      if (left === undefined || rightIndex === undefined) continue;
      const rawMargin = observation.raw_bb_per_hand;
      const directionFirstLogit =
        rawMargin === 0
          ? 0
          : Math.sign(rawMargin) *
            (BASE_WIN_LOGIT + MARGIN_BONUS_LOGIT * Math.tanh(Math.abs(rawMargin)));
      const difference = ELO_PER_LOGIT * directionFirstLogit;
      const weight = 1 / (OUTCOME_STANDARD_ERROR_ELO * OUTCOME_STANDARD_ERROR_ELO);
      normal[left][left] += weight;
      normal[rightIndex][rightIndex] += weight;
      normal[left][rightIndex] -= weight;
      normal[rightIndex][left] -= weight;
      right[left] += weight * difference;
      right[rightIndex] -= weight * difference;
    }

    for (let index = 0; index < size; index += 1) {
      normal[index][size] = 1;
      normal[size][index] = 1;
    }
    const solution = solveLinear(normal, right);
    ordered.forEach((botId, index) => ratings.set(botId, 1500 + solution[index]));
  }

  return [...dashboard.ratings]
    .filter((bot) => botIds.has(bot.bot_id))
    .map((bot) => ({ ...bot, elo: ratings.get(bot.bot_id) ?? 1500 }))
    .sort((left, right) => right.elo - left.elo || left.name.localeCompare(right.name));
}

export function resultTone(value: number) {
  const strength = Math.min(Math.abs(value) / 10, 1);
  if (value > 0) {
    return {
      background: `color-mix(in oklab, #18a56b ${22 + strength * 58}%, transparent)`,
      color: strength > 0.52 ? '#f4fff8' : 'inherit',
    };
  }
  if (value < 0) {
    return {
      background: `color-mix(in oklab, #d14f48 ${20 + strength * 57}%, transparent)`,
      color: strength > 0.52 ? '#fff7f6' : 'inherit',
    };
  }
  return { background: 'color-mix(in oklab, currentColor 5%, transparent)' };
}
