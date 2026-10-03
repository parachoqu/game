// Exporta os dados de design da demo para a reescrita em C++ (`cpp/data`).
//
//   node demo/tools/export-game-data.mjs           → grava cpp/data/*.json
//   node demo/tools/export-game-data.mjs --check   → só confere; sai com código 1 se algo mudou
//
// A fonte é `src/config.js` (ITEMS, WEAPONS, SKILLS, …) mais as constantes nomeadas que ficam nos
// módulos de jogo (WALK, RUN, GRAVITY, MAX_SLOPE, BASE_CAP…), lidas do próprio código-fonte para
// não haver uma segunda cópia digitada à mão. A saída separa três públicos:
//
//   cpp/data/*.json                 regras: o que o servidor precisa (pesos, danos, preços…)
//   cpp/data/presentation/*.json    aparência sem idioma: glifos e cores (só o cliente lê)
//   cpp/data/text/pt-BR/*.json      textos: nomes, descrições e regras escritas (só o cliente lê)
//
// Ids são as chaves do JS, na mesma ordem de declaração: a ordem importa para reproduzir o jogo.
import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

const TOOLS = dirname(fileURLToPath(import.meta.url));
const DEMO = dirname(TOOLS);
const ROOT = dirname(DEMO);
const OUT = join(ROOT, 'cpp', 'data');
const check = process.argv.includes('--check');

// Importado como `data:` URL, que é sempre ESM: funciona em qualquer Node ≥ 18, sem depender da
// detecção automática de módulos (Node ≥ 22.7). O config.js não importa nada, então isso basta.
const C = await import('data:text/javascript;charset=utf-8,' +
  encodeURIComponent(readFileSync(join(DEMO, 'src', 'config.js'), 'utf8')));

// ---------------------------------------------------------------- constantes nomeadas do código
// Lê `NOME = <expressão>` de um módulo e avalia a expressão com as constantes já lidas no escopo.
// Só aceita literais numéricos, objetos de números e aritmética sobre nomes já conhecidos.
function readConst(file, name, scope) {
  const src = readFileSync(join(DEMO, 'src', file), 'utf8');
  const m = new RegExp(`\\b(?:const|let)\\s+(?:[A-Za-z_$][\\w$]*\\s*=\\s*[^,;]+,\\s*)*${name}\\s*=\\s*`).exec(src);
  if (!m) throw new Error(`${file}: constante ${name} não encontrada`);
  let i = m.index + m[0].length, depth = 0, expr = '';
  for (; i < src.length; i++) {
    const ch = src[i];
    if (ch === '{' || ch === '(' || ch === '[') depth++;
    if (ch === '}' || ch === ')' || ch === ']') depth--;
    if (depth === 0 && (ch === ',' || ch === ';' || ch === '\n')) break;
    expr += ch;
  }
  expr = expr.trim();
  if (!/^[\w\s.:{}()+\-*/,]+$/.test(expr)) throw new Error(`${file}: expressão de ${name} fora do formato esperado: ${expr}`);
  const names = Object.keys(scope);
  // eslint-disable-next-line no-new-func
  const value = new Function(...names, `return (${expr});`)(...names.map((k) => scope[k]));
  scope[name] = value;
  return value;
}

const K = {};
for (const n of ['WALK', 'RUN', 'CROUCH', 'GRAVITY', 'JUMP', 'BOOST', 'AIR_CONTROL']) readConst('game/player.js', n, K);
for (const n of ['CELL', 'MAX_SLOPE', 'ROUTE_SLOPE', 'ROUTE_LANE']) readConst('game/collide.js', n, K);
for (const n of ['BASE_CAP', 'OVER_LIMIT']) readConst('game/inventory.js', n, K);
readConst('game/turbulent.js', 'DURATION', K);

// O guarda não está em ENEMIES: `spawnEnemy('guarda')` usa um objeto escrito na própria função.
function readGuard() {
  const src = readFileSync(join(DEMO, 'src', 'game', 'enemies.js'), 'utf8');
  const m = /type === 'guarda'\s*\?\s*(\{[^}]*\})/.exec(src);
  if (!m) throw new Error('enemies.js: definição inline do guarda não encontrada');
  // eslint-disable-next-line no-new-func
  return new Function(`return (${m[1]});`)();
}

// ---------------------------------------------------------------- utilidades
const pick = (o, keys) => Object.fromEntries(keys.filter((k) => o[k] !== undefined).map((k) => [k, o[k]]));
const mapValues = (o, f) => Object.fromEntries(Object.entries(o).map(([k, v]) => [k, f(v, k)]));
const need = (cond, msg) => { if (!cond) throw new Error(msg); };

// ---------------------------------------------------------------- regras
const ITEM_RULES = ['w', 'kind', 'family', 'req', 'hp', 'speed', 'cap'];
const items = mapValues(C.ITEMS, (it) => {
  const r = pick(it, ITEM_RULES);
  r.weight = r.w; delete r.w;
  return r;
});

const weapons = mapValues(C.WEAPONS, (w) => ({ basic: { ...w.basic }, skills: [...w.skills] }));
const skills = mapValues(C.SKILLS, (s) => pick(s, ['cost', 'cd']));
const trainings = mapValues(C.TRAININGS, (t) => pick(t, ['cost']));
const recipes = C.RECIPES.map((r) => ({ out: r.out, qty: r.qty, in: { ...r.in }, req: r.req }));
need(new Set(recipes.map((r) => r.out)).size === recipes.length, 'RECIPES: `out` deixou de ser único; o C++ usa `out` como id da receita');

const guard = readGuard();
const ENEMY_RULES = ['faction', 'hp', 'speed', 'dmg', 'range', 'arc', 'shape', 'windup', 'cd', 'aggro', 'drop', 'coins', 'ranged', 'weapon', 'scale'];
const enemies = { ...mapValues(C.ENEMIES, (e) => pick(e, ENEMY_RULES)), guarda: pick(guard, ENEMY_RULES) };

// Regra de perda por zona (zones.js `handleDeath`): protegida só desgasta, fronteira perde a carga,
// Full Loot e Turbulenta perdem tudo o que é levado.
const LOSS_RULE = { protegida: 'wear', fronteira: 'cargo', fullloot: 'full', turbulenta: 'full' };
const zones = mapValues(C.ZONES, (_, k) => { need(LOSS_RULE[k], `ZONES.${k}: regra de perda desconhecida`); return { loss: LOSS_RULE[k] }; });

const markets = {
  markets: mapValues(C.MARKETS, (m) => ({ buy: { ...m.buy }, sell: { ...m.sell } })),
  mountPrice: C.MOUNT_PRICE,
  restartKitPrice: C.RESTART_KIT_PRICE,
};
const origins = mapValues(C.ORIGINS, () => ({}));
// Ao criar o personagem, a chave do arquétipo entra em `trainings` (player.js createPlayer), mesmo
// quando não é um treino de TRAININGS (lanceiro, mago_gelo…). O C++ preserva isso.
const starts = mapValues(C.STARTS, (s) => ({ weapon: s.weapon }));

const balance = {
  clock: { dayLength: C.DAY_LENGTH, startHour: C.START_HOUR },
  movement: {
    walk: K.WALK, run: K.RUN, crouch: K.CROUCH, gravity: K.GRAVITY, airControl: K.AIR_CONTROL,
    jump: { vy: K.JUMP.vy, vigor: K.JUMP.vigor },
    boost: { vy: K.BOOST.vy, vigor: K.BOOST.vigor, speed: K.BOOST.speed },
  },
  collision: { cell: K.CELL, maxSlope: K.MAX_SLOPE, routeSlope: K.ROUTE_SLOPE, routeLane: K.ROUTE_LANE },
  inventory: { baseCapacity: K.BASE_CAP, overLimit: K.OVER_LIMIT },
  turbulent: { duration: K.DURATION },
};

// ---------------------------------------------------------------- aparência e textos
const presentation = {
  items: mapValues(C.ITEMS, (it) => pick(it, ['glyph', 'color'])),
  zones: mapValues(C.ZONES, (z) => pick(z, ['color', 'mark'])),
  origins: mapValues(C.ORIGINS, (o) => pick(o, ['skin', 'cloth', 'trim'])),
};
const text = {
  items: mapValues(C.ITEMS, (it) => pick(it, ['name', 'desc'])),
  weapons: mapValues(C.WEAPONS, (w) => pick(w, ['name'])),
  skills: mapValues(C.SKILLS, (s) => pick(s, ['name', 'desc'])),
  trainings: mapValues(C.TRAININGS, (t) => pick(t, ['name', 'desc'])),
  recipes: Object.fromEntries(C.RECIPES.map((r) => [r.out, pick(r, ['note'])])),
  enemies: { ...mapValues(C.ENEMIES, (e) => pick(e, ['name'])), guarda: pick(guard, ['name']) },
  zones: mapValues(C.ZONES, (z) => pick(z, ['name', 'rule', 'death'])),
  markets: mapValues(C.MARKETS, (m) => pick(m, ['name'])),
  origins: mapValues(C.ORIGINS, (o) => pick(o, ['name', 'hint'])),
  starts: mapValues(C.STARTS, (s) => pick(s, ['label', 'desc'])),
};

// ---------------------------------------------------------------- escrita
const files = {
  'items.json': items, 'weapons.json': weapons, 'skills.json': skills, 'trainings.json': trainings,
  'recipes.json': recipes, 'enemies.json': enemies, 'zones.json': zones, 'markets.json': markets,
  'origins.json': origins, 'starts.json': starts, 'balance.json': balance,
  ...Object.fromEntries(Object.entries(presentation).map(([k, v]) => [`presentation/${k}.json`, v])),
  ...Object.fromEntries(Object.entries(text).map(([k, v]) => [`text/pt-BR/${k}.json`, v])),
};

let changed = 0;
for (const [name, value] of Object.entries(files)) {
  const path = join(OUT, name);
  const body = JSON.stringify(value, null, 2) + '\n';
  // Checkouts no Windows podem trazer CRLF: compara o conteúdo, não o fim de linha.
  const old = existsSync(path) ? readFileSync(path, 'utf8').replace(/\r\n/g, '\n') : null;
  if (old === body) continue;
  changed++;
  if (check) { console.error(`desatualizado: ${relative(ROOT, path)}`); continue; }
  mkdirSync(dirname(path), { recursive: true });
  writeFileSync(path, body);
  console.log(`gravado  ${relative(ROOT, path)}`);
}
if (check && changed) {
  console.error(`${changed} arquivo(s) de cpp/data diferem de demo/src. Rode: node demo/tools/export-game-data.mjs`);
  process.exit(1);
}
console.log(check ? 'cpp/data em dia com demo/src' : `${changed} arquivo(s) atualizado(s) em cpp/data`);
