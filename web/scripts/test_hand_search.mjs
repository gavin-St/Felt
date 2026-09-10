import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import ts from 'typescript';

// Transpile the two browser modules in memory; no DOM or storage mocks are
// needed for the URL/snapshot contract. Only Vite's environment is supplied.
const moduleUrl = (source) => `data:text/javascript;base64,${Buffer.from(ts.transpileModule(source, {
  compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ESNext },
}).outputText).toString('base64')}`;
const hands = moduleUrl((await readFile(new URL('../lib/hands.ts', import.meta.url), 'utf8'))
  .replaceAll('import.meta.env.DEV', 'false'));
const source = (await readFile(new URL('../lib/hand-search-state.ts', import.meta.url), 'utf8'))
  .replace("'./hands'", JSON.stringify(hands));
const { restoreHandSearch, handSearchUrl } = await import(moduleUrl(source));
const saved = { bot: 47, opponent: 48, hand: 'T9s', filters: ['showdown', 'won'],
  sort: 'pot', offset: 20, from: '/bot/47' };
const restore = (query, raw = JSON.stringify(saved)) => restoreHandSearch(new URLSearchParams(query), raw);
assert.deepEqual(restore(''), saved);
assert.deepEqual(restore('bot=46'), { ...saved, bot: 46 });
assert.deepEqual(restore(handSearchUrl(saved).split('?')[1]), saved);
const cleared = { hand: '', filters: [], sort: 'played', offset: 0 };
assert.deepEqual(restore(handSearchUrl(cleared).split('?')[1]),
  { ...cleared, bot: undefined, opponent: undefined, from: undefined });
assert.equal(restore('offset=0').offset, 0);
assert.deepEqual(restore('filters=').filters, []);
assert.equal(restore('hand=').hand, '');
assert.equal(restore('opponent=').opponent, undefined);
for (const raw of ['null', '[]', 'broken', '17', '{"filters":[7,"unknown"],"sort":"unknown","offset":-3}']) {
  const state = restore('', raw);
  assert.deepEqual(state.filters, []);
  assert.equal(state.sort, 'played');
  assert.equal(state.offset, 0);
}
assert.deepEqual(restore('filters=showdown,preflop,unknown').filters, ['showdown']);
assert.equal(restore('from=//external.example').from, undefined);
for (const sort of ['played', 'pot', 'won', 'lost', 'random']) {
  const original = restore(`sort=${sort}`);
  assert.deepEqual(restore(handSearchUrl(original).split('?')[1], null), original);
}
console.log('Hand-search URL, cleared fields, saved state, pagination, and corrupt-storage regressions passed.');
