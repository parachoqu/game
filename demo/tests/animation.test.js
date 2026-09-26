// Testes do lote Mixamo de combate no runtime: registro, seleção de clipes e separação entre
// humanoides e criaturas. Rodam pelo caminho do build (esbuild com os loaders de .glb):
//   node tools/run-animation-tests.mjs
import {
  toLocal, locomotionWeights, dodgeClip, DODGE_FACING, hitSide, HIT_CLIPS, hitClip, deathFall, deathClip,
} from '../src/engine/anim-select.js';
import COMBAT_CLIPS, { COMBAT_BATCH } from '../src/engine/combat-clips.js';
import EXTRA_CLIPS from '../src/engine/extra-clips.js';
import { CLIP_REGISTRY } from '../src/engine/models.js';
import { enemyAnimState } from '../src/game/enemies.js';
import { COMBAT_2026_09_26, COMBAT_BATCH as MANIFEST_BATCH } from '../tools/combat-manifest.mjs';

const results = [];
function test(name, fn) {
  try { fn(); results.push({ name, ok: true }); } catch (e) { results.push({ name, ok: false, error: e.message }); }
}
function assert(cond, msg) { if (!cond) throw new Error(msg || 'falhou'); }
function close(a, b, tol, msg) {
  if (Math.abs(a - b) > tol) throw new Error(`${msg || 'valor'}: ${a} ≠ ${b} (tolerância ${tol})`);
}
const all = (keys) => (k) => keys.includes(k);
const FULL = all(['walk', 'walkBack', 'strafeLeft', 'strafeRight', 'roll', 'down',
  'dodgeForward', 'dodgeBackward', 'dodgeLeft', 'dodgeRight', 'hitFront', 'hitBack', 'hitLeft', 'hitRight',
  'deathForward', 'deathBackward']);

// ---------------------------------------------------------------- registro
test('lote: registro gerado igual ao manifesto (20 entradas, mesma ordem e modos)', () => {
  assert(COMBAT_BATCH === MANIFEST_BATCH && COMBAT_BATCH === 'COMBAT_2026_09_26', `lote ${COMBAT_BATCH}`);
  assert(COMBAT_2026_09_26.length === 20, `manifesto com ${COMBAT_2026_09_26.length} entradas`);
  assert(COMBAT_CLIPS.length === 20, `registro com ${COMBAT_CLIPS.length} entradas`);
  COMBAT_2026_09_26.forEach((m, i) => {
    const r = COMBAT_CLIPS[i];
    for (const f of ['key', 'glb', 'source', 'contactMode', 'locomotionMode']) assert(r[f] === m[f], `${m.key}: ${f} ${r[f]} ≠ ${m[f]}`);
    assert(r.bin instanceof Uint8Array && r.bin.byteLength > 1000, `${m.key}: GLB importado vazio`);
  });
});

test('lote: arquivo → chave → GLB sem colisões, nem com a biblioteca e os opcionais', () => {
  for (const f of ['source', 'key', 'glb']) {
    const v = COMBAT_2026_09_26.map((e) => e[f]);
    assert(new Set(v).size === 20, `${f} repetido`);
  }
  const lib = CLIP_REGISTRY.filter((e) => e.group !== 'combat').map((e) => e.key);
  for (const e of COMBAT_2026_09_26) assert(!lib.includes(e.key), `chave ${e.key} colide com a biblioteca`);
  // cada GLB importado é um arquivo diferente (bytes distintos)
  const sig = new Set(COMBAT_CLIPS.map((e) => `${e.bin.byteLength}:${e.bin.slice(-64).join(',')}`));
  assert(sig.size === 20, 'dois registros apontam para o mesmo GLB');
});

test('registro único: biblioteca, opcionais e lote no mesmo formato', () => {
  const keys = CLIP_REGISTRY.map((e) => e.key);
  assert(new Set(keys).size === keys.length, 'chave repetida no registro');
  for (const e of CLIP_REGISTRY) {
    assert(e.bin instanceof Uint8Array, `${e.key}: sem GLB`);
    assert(['feet', 'body', 'none'].includes(e.contactMode), `${e.key}: contactMode ${e.contactMode}`);
    assert(['cycle', 'pose', 'oneshot'].includes(e.locomotionMode), `${e.key}: locomotionMode ${e.locomotionMode}`);
    assert(['library', 'extra', 'combat'].includes(e.group), `${e.key}: grupo ${e.group}`);
  }
  const combat = CLIP_REGISTRY.filter((e) => e.group === 'combat');
  assert(combat.length === 20, `${combat.length} clipes do lote no registro`);
  assert(combat.every((e) => e.refFrom === 'rest'), 'o lote usa o repouso do esqueleto de origem');
  for (const k of Object.keys(EXTRA_CLIPS)) assert(CLIP_REGISTRY.some((e) => e.key === k && e.refFrom === 'rest'), `opcional ${k} fora do registro`);
  for (const k of ['walk', 'run', 'idle', 'roll', 'down']) assert(CLIP_REGISTRY.some((e) => e.key === k), `biblioteca sem ${k}`);
  // poses que passam pelo chão assentam pelo corpo; o resto do lote, pelos pés
  for (const e of combat) assert(e.contactMode === (/^(getUp|death)/.test(e.key) ? 'body' : 'feet'), `${e.key}: contato ${e.contactMode}`);
});

// ---------------------------------------------------------------- locomoção direcional
test('locomoção: frente, trás, esquerda e direita', () => {
  const w = (f, l) => locomotionWeights(f, l, FULL);
  close(w(1, 0).walk, 1, 1e-9, 'frente');
  close(w(-1, 0).walkBack, 1, 1e-9, 'trás');
  close(w(0, 1).strafeLeft, 1, 1e-9, 'esquerda');
  close(w(0, -1).strafeRight, 1, 1e-9, 'direita');
  assert(!w(-1, 0).reverse, 'com o clipe para trás a caminhada não é invertida');
});

test('locomoção: diagonais dividem o peso e a soma é sempre 1', () => {
  const d = locomotionWeights(1, 1, FULL);
  close(d.walk, 0.5, 1e-9, 'frente-esquerda (frente)'); close(d.strafeLeft, 0.5, 1e-9, 'frente-esquerda (lado)');
  const e = locomotionWeights(-1, -1, FULL);
  close(e.walkBack, 0.5, 1e-9, 'trás-direita (trás)'); close(e.strafeRight, 0.5, 1e-9, 'trás-direita (lado)');
  const g = locomotionWeights(0.9, -0.3, FULL);
  assert(g.walk > g.strafeRight && g.strafeRight > 0 && g.walkBack === 0 && g.strafeLeft === 0, 'quase frente, um pouco à direita');
  for (let a = 0; a < Math.PI * 2; a += 0.1) {
    const r = locomotionWeights(Math.cos(a), Math.sin(a), FULL);
    close(r.walk + r.walkBack + r.strafeLeft + r.strafeRight, 1, 1e-9, `soma em ${a.toFixed(1)} rad`);
  }
  close(locomotionWeights(0, 0, FULL).walk, 1, 0, 'parado');
});

test('locomoção: registro parcial volta à caminhada (ao contrário, sem o clipe para trás)', () => {
  const noBack = all(['walk', 'strafeLeft', 'strafeRight']);
  const b = locomotionWeights(-1, 0, noBack);
  close(b.walk, 1, 1e-9, 'trás sem clipe'); assert(b.reverse, 'caminhada invertida');
  const noSide = all(['walk', 'walkBack']);
  const s = locomotionWeights(0, 1, noSide);
  close(s.walk, 1, 1e-9, 'lado sem clipe'); assert(!s.reverse, 'lado sem clipe anda para frente');
  const mix = locomotionWeights(-1, 1, noBack);
  close(mix.walk, 0.5, 1e-9, 'trás-esquerda sem trás'); close(mix.strafeLeft, 0.5, 1e-9, 'trás-esquerda sem trás (lado)');
  assert(mix.reverse, 'parte de trás na caminhada invertida');
});

// ---------------------------------------------------------------- esquiva
test('esquiva: quatro direções, e o rolamento como substituto', () => {
  assert(dodgeClip(1, 0, FULL) === 'dodgeForward', 'frente');
  assert(dodgeClip(-1, 0, FULL) === 'dodgeBackward', 'trás');
  assert(dodgeClip(0, 1, FULL) === 'dodgeLeft', 'esquerda');
  assert(dodgeClip(0, -1, FULL) === 'dodgeRight', 'direita');
  assert(dodgeClip(0.7, 0.72, FULL) === 'dodgeLeft', 'diagonal: eixo dominante');
  assert(dodgeClip(0, 1, FULL, false) === 'roll', 'arma incompatível → rolamento');
  assert(dodgeClip(0, 1, all(['roll', 'dodgeRight'])) === 'roll', 'sem o clipe da direção → rolamento');
  assert(dodgeClip(0, 1, all([])) === null, 'sem nenhum clipe');
});

test('esquiva: o giro do corpo faz o clipe mostrar o sentido do deslocamento', () => {
  const want = { dodgeForward: [1, 0], dodgeBackward: [-1, 0], dodgeLeft: [0, 1], dodgeRight: [0, -1], roll: [1, 0] };
  for (let yaw = -3; yaw <= 3; yaw += 0.7) {
    for (const [key, [f, l]] of Object.entries(want)) {
      const face = yaw - DODGE_FACING[key];
      const d = toLocal(face, Math.sin(yaw), Math.cos(yaw));
      close(d.fwd, f, 1e-9, `${key} frente`); close(d.left, l, 1e-9, `${key} lado`);
    }
  }
});

// ---------------------------------------------------------------- impacto e morte
test('impacto: lado da origem do golpe no referencial da vítima', () => {
  // vítima na origem olhando para +z: +x é a esquerda dela
  assert(hitSide(0, 0, 0, { x: 0, z: 5 }) === 'front', 'frente');
  assert(hitSide(0, 0, 0, { x: 0, z: -5 }) === 'back', 'trás');
  assert(hitSide(0, 0, 0, { x: 5, z: 0 }) === 'left', 'esquerda');
  assert(hitSide(0, 0, 0, { x: -5, z: 0 }) === 'right', 'direita');
  // olhando para +x: +z fica à direita; a posição da vítima conta
  assert(hitSide(Math.PI / 2, 10, 10, { x: 15, z: 10 }) === 'front', 'girada: frente');
  assert(hitSide(Math.PI / 2, 10, 10, { x: 10, z: 15 }) === 'right', 'girada: direita');
  assert(hitSide(Math.PI / 2, 10, 10, { x: 10, z: 5 }) === 'left', 'girada: esquerda');
  assert(hitSide(0, 0, 0, { x: 3, z: 2 }) === 'left', 'eixo dominante');
  assert(hitSide(0, 0, 0, null) === 'front' && hitSide(0, 0, 0, { x: NaN, z: 1 }) === 'front', 'sem origem → frente');
  assert(hitSide(0, 0, 0, { x: 0, z: 0 }) === 'front', 'origem coincidente → frente');
});

test('impacto: tabela lado → clipe e registro parcial', () => {
  assert(HIT_CLIPS.front === 'hitFront' && HIT_CLIPS.back === 'hitBack' && HIT_CLIPS.left === 'hitLeft' && HIT_CLIPS.right === 'hitRight', 'tabela');
  for (const side of ['front', 'back', 'left', 'right']) assert(hitClip(side, FULL) === HIT_CLIPS[side], side);
  assert(hitClip('left', all(['hitFront'])) === null, 'sem o clipe do lado, sem reação');
});

test('morte: cai para trás com golpe pela frente e para frente com golpe por trás', () => {
  assert(deathFall(0, 0, 0, { x: 0, z: 4 }) === 'backward', 'golpe pela frente');
  assert(deathFall(0, 0, 0, { x: 0, z: -4 }) === 'forward', 'golpe por trás');
  assert(deathFall(0, 0, 0, { x: 4, z: 0.5 }) === 'backward', 'lado, meio da frente');
  assert(deathFall(0, 0, 0, { x: 4, z: -0.5 }) === 'forward', 'lado, meio de trás');
  assert(deathFall(0, 0, 0, undefined) === 'backward', 'sem origem → para trás');
  assert(deathClip('forward', FULL) === 'deathForward' && deathClip('backward', FULL) === 'deathBackward', 'clipes');
  assert(deathClip('forward', all(['down'])) === 'down', 'sem o clipe direcional → derrubado');
  assert(deathClip('forward', all([])) === null, 'sem nenhum clipe');
});

// ---------------------------------------------------------------- humanoides × criaturas
const cfg = { speed: 5, windup: 0.6, ranged: false };
const enemy = (kind, extra = {}) => ({ model: { kind, m: {} }, cfg, state: 'chase', t: 0.4, atk: -1, alive: true, ...extra });

test('criaturas: estado da leoa não recebe impacto nem morte humanos', () => {
  const b = enemyAnimState(enemy('beast', { hitAt: -0.1, hitDir: 'left', deathDir: 'forward' }), 2, 0.016);
  assert(b.animator === 'beast', 'animador da criatura');
  for (const k of ['hit', 'death', 'dodgeKey']) assert(!(k in b.s), `criatura recebeu ${k}`);
  const dead = enemyAnimState(enemy('beast', { alive: false, deathDir: 'forward' }), 0, 0.016);
  assert(dead.animator === 'beast' && dead.s.down === true && !('death' in dead.s), 'criatura morta segue a derrubada genérica');
});

test('humanoides: morte direcional separada da derrubada recuperável', () => {
  const alive = enemyAnimState(enemy('human'), 3, 0.016);
  assert(alive.animator === 'human' && alive.s.down === false && alive.s.death === null, 'vivo');
  const dead = enemyAnimState(enemy('human', { alive: false, t: 1.2, deathDir: 'forward' }), 0, 0.016);
  assert(dead.s.down === false, 'morte não chega como derrubada');
  assert(dead.s.death && dead.s.death.fall === 'forward' && dead.s.death.t === 1.2, 'morte com lado e tempo');
  const noDir = enemyAnimState(enemy('human', { alive: false, t: 0.5 }), 0, 0.016);
  assert(noDir.s.death.fall === 'backward', 'sem lado registrado → para trás');
  assert(noDir.s.hit === null, 'morto não reage a golpe');
});

// ---------------------------------------------------------------- relatório
const failed = results.filter((r) => !r.ok);
for (const r of results) console.log(`${r.ok ? '  ok  ' : ' FALHA'} ${r.name}${r.ok ? '' : ` — ${r.error}`}`);
console.log(`\n${results.length - failed.length}/${results.length} testes aprovados`);
if (failed.length) {
  console.log('FALHAS:');
  for (const r of failed) console.log(` - ${r.name}: ${r.error}`);
}
globalThis.__testExit = failed.length ? 1 : 0;
