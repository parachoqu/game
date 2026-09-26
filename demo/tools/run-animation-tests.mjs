// Testes do lote Mixamo de combate (COMBAT_2026_09_26).
//
//   node tools/run-animation-tests.mjs
//
// Parte 1, aqui no Node: fontes (hashes), derivados GLB (um clipe, sem malha, quadril no lugar em
// todas as amostras, refY preservado) e o processador (falha com fonte ausente, alterada ou saída
// inválida; modos exclusivos). Parte 2: `tests/animation.test.js`, empacotado pelo mesmo caminho do
// build (registro, seleção de clipes, humanoides × criaturas).
import { spawnSync } from 'node:child_process';
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Document, NodeIO } from '@gltf-transform/core';
import { ALL_EXTENSIONS } from '@gltf-transform/extensions';
import { cloneDocument } from '@gltf-transform/functions';
import { MeshoptDecoder } from 'meshoptimizer';
import { COMBAT_2026_09_26, COMBAT_DIR } from './combat-manifest.mjs';
import { validateClipDoc, sha256, sha256File, hipsRestY } from './anim-pipeline.mjs';

const TOOLS = dirname(fileURLToPath(import.meta.url));
const DEMO = dirname(TOOLS);
const SRC = join(DEMO, '..', 'modelos 3d animados', 'animacoes', COMBAT_DIR);
const LIB = join(DEMO, '..', 'modelos 3d animados', 'animacoes', 'glb');
const ASSETS = join(DEMO, 'assets');

await MeshoptDecoder.ready;
const io = new NodeIO().registerExtensions(ALL_EXTENSIONS).registerDependencies({ 'meshopt.decoder': MeshoptDecoder });

const results = [];
async function test(name, fn) {
  try { await fn(); results.push({ name, ok: true }); } catch (e) { results.push({ name, ok: false, error: e.message }); }
}
function assert(cond, msg) { if (!cond) throw new Error(msg || 'falhou'); }
const processor = (args, env = {}) => spawnSync(process.execPath, [join(TOOLS, 'process-animations.mjs'), ...args], {
  env: { ...process.env, ...env }, encoding: 'utf8', timeout: 300000,
});

// ---------------------------------------------------------------- fontes
await test('fontes: exatamente os 20 FBX do manifesto, com o hash recebido', () => {
  const files = readdirSync(SRC).filter((f) => f.toLowerCase().endsWith('.fbx')).sort();
  const want = COMBAT_2026_09_26.map((e) => e.source).sort();
  assert(files.length === 20, `${files.length} FBX na pasta do lote`);
  assert(JSON.stringify(files) === JSON.stringify(want), 'arquivos da pasta ≠ manifesto');
  for (const e of COMBAT_2026_09_26) assert(sha256File(join(SRC, e.source)) === e.sha256, `${e.source} mudou (sha256)`);
});

// ---------------------------------------------------------------- derivados
const docs = new Map();
await test('derivados: 20 GLB em assets/ e em animacoes/glb/, idênticos e distintos entre si', async () => {
  const seen = new Map();
  for (const e of COMBAT_2026_09_26) {
    const a = join(ASSETS, e.glb), l = join(LIB, e.glb);
    assert(existsSync(a) && existsSync(l), `${e.glb} ausente`);
    const ha = sha256(readFileSync(a));
    assert(ha === sha256(readFileSync(l)), `${e.glb}: cópias diferentes`);
    assert(!seen.has(ha), `${e.glb} idêntico a ${seen.get(ha)}`);
    seen.set(ha, e.glb);
    docs.set(e.key, await io.read(a));
  }
});

await test('derivados: um clipe não vazio com o nome da chave, sem malha, pele nem material', () => {
  for (const e of COMBAT_2026_09_26) {
    const errs = validateClipDoc(docs.get(e.key), e.key);
    assert(!errs.length, `${e.key}: ${errs.join('; ')}`);
  }
});

await test('derivados: amostrados a 30 fps (bake30)', () => {
  for (const e of COMBAT_2026_09_26) {
    const a = docs.get(e.key).getRoot().listAnimations()[0];
    // depois do resample sobram menos chaves, mas os instantes continuam na grade de 1/30 s
    for (const s of a.listSamplers()) for (const t of s.getInput().getArray()) {
      assert(Math.abs(t * 30 - Math.round(t * 30)) < 0.02, `${e.key}: chave fora da grade de 30 fps (${t})`);
    }
  }
});

await test('contrato no lugar: X/Z do quadril fixos no repouso em todas as amostras; refY preservado', () => {
  let refY = null;
  for (const e of COMBAT_2026_09_26) {
    const doc = docs.get(e.key);
    const hips = doc.getRoot().listNodes().find((n) => /Hips$/.test(n.getName()));
    const rest = hips.getTranslation(), y = hipsRestY(doc);
    assert(y > 0.5 && y < 1.2, `${e.key}: refY ${y}`);
    refY ??= y;
    assert(Math.abs(y - refY) < 1e-6, `${e.key}: refY ${y} ≠ ${refY} (mesmo esqueleto de origem)`);
    const ch = doc.getRoot().listAnimations()[0].listChannels().find((c) => c.getTargetNode() === hips && c.getTargetPath() === 'translation');
    if (!ch) continue;
    const v = ch.getSampler().getOutput().getArray();
    for (let i = 0; i < v.length; i += 3) {
      assert(Math.abs(v[i] - rest[0]) < 1e-4 && Math.abs(v[i + 2] - rest[2]) < 1e-4, `${e.key}: amostra ${i / 3} fora do lugar`);
    }
    // a altura continua: agachar, cair e levantar descem o quadril
    if (/^(getUp|death|crouchTo|standTo)/.test(e.key)) {
      let lo = Infinity, hi = -Infinity;
      for (let i = 1; i < v.length; i += 3) { lo = Math.min(lo, v[i]); hi = Math.max(hi, v[i]); }
      assert(hi - lo > 0.3 * y, `${e.key}: altura do quadril achatada (${(hi - lo).toFixed(3)})`);
    }
  }
});

// ---------------------------------------------------------------- processador
await test('processador: saída inválida é recusada pela validação', async () => {
  const good = docs.get('hitFront');
  assert(!validateClipDoc(good, 'hitFront').length, 'derivado bom recusado');
  assert(validateClipDoc(good, 'hitBack').some((m) => /esperado "hitBack"/.test(m)), 'nome errado aceito');
  const bad = cloneDocument(good);
  bad.getRoot().listScenes()[0].addChild(bad.createNode('malha').setMesh(bad.createMesh('m')));
  bad.createMaterial('mat');
  const hips = bad.getRoot().listNodes().find((n) => /Hips$/.test(n.getName()));
  const ch = bad.getRoot().listAnimations()[0].listChannels().find((c) => c.getTargetNode() === hips && c.getTargetPath() === 'translation');
  const v = ch.getSampler().getOutput().getArray().slice(); v[v.length - 3] += 0.5; ch.getSampler().getOutput().setArray(v);
  const errs = validateClipDoc(bad, 'hitFront');
  for (const re of [/malha/, /material/, /X\/Z/]) assert(errs.some((m) => re.test(m)), `não acusou ${re}`);
  const two = cloneDocument(good);
  two.createAnimation('extra');
  assert(validateClipDoc(two, 'hitFront').some((m) => /2 clipes/.test(m)), 'dois clipes aceitos');
  const empty = new Document();
  assert(validateClipDoc(empty, 'x').length >= 2, 'documento vazio aceito');
});

const tmp = mkdtempSync(join(tmpdir(), 'rpg-anim-test-'));
try {
  await test('processador: fonte ausente → código ≠ 0 e nada escrito', () => {
    const dir = join(tmp, 'ausente', COMBAT_DIR);
    mkdirSync(dir, { recursive: true });
    for (const e of COMBAT_2026_09_26.slice(1)) copyFileSync(join(SRC, e.source), join(dir, e.source));
    const r = processor(['--combat', '--dry-run'], { ANIM_SRC_DIR: join(tmp, 'ausente') });
    assert(r.status === 1, `código ${r.status}`);
    assert(/fonte ausente: Ataque_Alto/.test(r.stderr) && /Nada foi escrito/.test(r.stderr), r.stderr.slice(0, 300));
  });

  await test('processador: fonte alterada ou inválida → código ≠ 0', () => {
    const dir = join(tmp, 'alterada', COMBAT_DIR);
    mkdirSync(dir, { recursive: true });
    for (const e of COMBAT_2026_09_26) copyFileSync(join(SRC, e.source), join(dir, e.source));
    writeFileSync(join(dir, COMBAT_2026_09_26[3].source), 'não é um FBX');
    const r = processor(['--combat', '--dry-run'], { ANIM_SRC_DIR: join(tmp, 'alterada') });
    assert(r.status === 1, `código ${r.status}`);
    assert(/fonte alterada/.test(r.stderr), r.stderr.slice(0, 300));
  });

  await test('processador: modos exclusivos e opções inválidas → código 2', () => {
    assert(processor(['--extra', '--combat']).status === 2, '--extra --combat aceito');
    assert(processor(['--combate']).status === 2, 'opção desconhecida aceita');
    assert(processor(['--combat'], { ANIM_SRC_DIR: tmp }).status === 2, 'ANIM_SRC_DIR sem --dry-run aceito');
  });
} finally {
  rmSync(tmp, { recursive: true, force: true });
}

// ---------------------------------------------------------------- parte 2: runtime empacotado
const bundled = spawnSync(process.execPath, [join(TOOLS, 'run-world-tests.mjs')], {
  cwd: DEMO, env: { ...process.env, TEST_ENTRY: 'tests/animation.test.js' }, encoding: 'utf8', timeout: 300000,
});
const m = /(\d+)\/(\d+) testes aprovados/.exec(bundled.stdout || '');

// ---------------------------------------------------------------- relatório
const failed = results.filter((r) => !r.ok);
console.log('Node (fontes, derivados, processador):');
for (const r of results) console.log(`${r.ok ? '  ok  ' : ' FALHA'} ${r.name}${r.ok ? '' : ` — ${r.error}`}`);
console.log('\nRuntime (tests/animation.test.js):');
process.stdout.write((bundled.stdout || '').replace(/\n\d+\/\d+ testes aprovados\n?/, '\n'));
if (bundled.stderr) process.stderr.write(bundled.stderr);
const okBundled = m ? +m[1] : 0, totBundled = m ? +m[2] : 0;
const ok = results.length - failed.length + okBundled, total = results.length + totBundled;
console.log(`\n${ok}/${total} testes aprovados (${results.length - failed.length}/${results.length} no Node, ${okBundled}/${totBundled} no runtime)`);
process.exit(failed.length || bundled.status !== 0 || !m || okBundled !== totBundled ? 1 : 0);
