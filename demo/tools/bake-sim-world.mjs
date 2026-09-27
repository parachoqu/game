// Pacote de mundo da simulação em C++ (cpp/assets/sim) e fixtures de paridade (cpp/tests/parity).
//
//   node demo/tools/bake-sim-world.mjs           → grava os arquivos
//   node demo/tools/bake-sim-world.mjs --check   → só confere; sai com código 1 se algo mudou
//
// Em vez de reescrever em C++ o que o carregamento do mundo faz (escavar o rio, espalhar ~22 mil
// árvores pelo kit de natureza, montar colisores de construções, pontes e objetos de jogo), este
// script roda a própria sequência de boot da demo no Node, com um ambiente mínimo de navegador, e
// grava o resultado. O servidor em C++ só lê os dados prontos; a demo não é alterada.
//
// Um plugin do esbuild acrescenta, só neste pacote, exportações de estruturas internas da demo
// (a grade de colisores na ordem de inserção, a máscara de biomas já escavada, o campo de rotas e os
// pontos de zona). Se algum desses trechos mudar em demo/src, o plugin falha em vez de gerar lixo.
//
// Requer os arquivos do Git LFS de demo/assets (modelos e kit de natureza) e `npm ci` em demo/tools.
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { build } from 'esbuild';
import { installBrowserShim } from './lib/node-env.mjs';

const TOOLS = dirname(fileURLToPath(import.meta.url));
const DEMO = dirname(TOOLS);
const ROOT = dirname(DEMO);
const OUT_SIM = join(ROOT, 'cpp', 'assets', 'sim');
const OUT_FIXTURES = join(ROOT, 'cpp', 'tests', 'parity', 'fixtures');
const OUT_TEXT = join(ROOT, 'cpp', 'data', 'text', 'pt-BR');
const check = process.argv.includes('--check');
const quiet = process.argv.includes('--quiet');

// ---------------------------------------------------------------- exportações só do pacote
const PATCHES = [
  {
    file: /[\\/]src[\\/]game[\\/]collide\.js$/,
    apply: (s) => replaceOnce(s, 'function insert(o) {', 'export const __inserted = [];\nfunction insert(o) { __inserted.push(o);'),
  },
  {
    file: /[\\/]src[\\/]world[\\/]heightfield\.js$/,
    apply: (s) => append(s, ['const MASK =', 'const MASK_W ='], 'export { MASK as __MASK, MASK_W as __MASK_W };'),
  },
  {
    file: /[\\/]src[\\/]world[\\/]world-routes\.js$/,
    apply: (s) => append(s, ['const routeField =', 'const RF_STEP =', 'const RF_N =', 'const RF_MAX ='],
      'export { routeField as __routeField, RF_STEP as __RF_STEP, RF_N as __RF_N, RF_MAX as __RF_MAX };'),
  },
  {
    file: /[\\/]src[\\/]game[\\/]layout\.js$/,
    apply: (s) => append(s, ['const PROTECTED_POINTS =', 'const NEVER_PROTECTED ='],
      'export { PROTECTED_POINTS as __PROTECTED, NEVER_PROTECTED as __NEVER_PROTECTED };'),
  },
];
function replaceOnce(src, needle, replacement) {
  const n = src.split(needle).length - 1;
  if (n !== 1) throw new Error(`bake: esperado exatamente um "${needle}" (achados: ${n}); a demo mudou`);
  return src.replace(needle, replacement);
}
function append(src, mustHave, line) {
  for (const m of mustHave) if (!src.includes(m)) throw new Error(`bake: "${m}" não existe mais na demo`);
  return `${src}\n${line}\n`;
}
const exportsPlugin = {
  name: 'bake-exports',
  setup(b) {
    for (const p of PATCHES) {
      b.onLoad({ filter: p.file }, (args) => ({ contents: p.apply(readFileSync(args.path, 'utf8')), loader: 'js' }));
    }
  },
};

// ---------------------------------------------------------------- empacotar e carregar o mundo
const bundle = join(TOOLS, 'bake', '.sim-world.bundle.mjs');
await build({
  absWorkingDir: DEMO,
  entryPoints: ['tools/bake/sim-world.entry.js'],
  bundle: true, format: 'esm', platform: 'neutral', target: 'es2022',
  alias: { three: './vendor/three/three.module.js' },
  loader: { '.glb': 'binary', '.bin': 'binary', '.json': 'json', '.webp': 'dataurl' },
  outfile: bundle, plugins: [exportsPlugin], logLevel: 'warning',
});
installBrowserShim({ loadImages: true });
// three.js avisa que texturas decodificadas no ambiente falso não serializam; não interessa aqui
const warn = console.warn;
console.warn = (...a) => { if (!String(a[0]).includes('Unable to serialize Texture')) warn(...a); };

let world;
try {
  world = await import(pathToFileURL(bundle).href);
  const t0 = Date.now();
  await world.loadWorld((m) => { if (!quiet) console.log(`  ${m}`); });
  if (!quiet) console.log(`mundo carregado em ${Date.now() - t0} ms`);
} finally {
  rmSync(bundle, { force: true });
}
const snap = world.snapshot();
const fix = world.fixtures();

// ---------------------------------------------------------------- serialização
const le = (TypedArray, arr) => Buffer.from(new TypedArray(arr).buffer);   // x86/ARM: little-endian
const sha = (buf) => createHash('sha256').update(buf).digest('hex');

// colisores: "RPGC", versão, quantidade; depois cada um com tipo (0 círculo, 1 caixa) e os campos em
// float64, na ordem de inserção (a ordem das listas por célula decide o empurrão em `resolve`).
function collidersBin(list) {
  const size = 12 + list.reduce((n, o) => n + 1 + (o.t === 0 ? 3 : 7) * 8, 0);
  const buf = Buffer.alloc(size);
  buf.write('RPGC', 0, 'ascii');
  buf.writeUInt32LE(1, 4);
  buf.writeUInt32LE(list.length, 8);
  let at = 12;
  for (const o of list) {
    buf.writeUInt8(o.t, at++);
    const fields = o.t === 0 ? [o.x, o.z, o.r] : [o.x, o.z, o.hw, o.hd, o.c, o.s, o.r];
    for (const v of fields) { buf.writeDoubleLE(v, at); at += 8; }
  }
  return buf;
}

const files = {
  'region-height.u16': Buffer.from(snap.region.height.data.buffer, snap.region.height.data.byteOffset, snap.region.height.data.byteLength),
  'turbulent-height.u16': Buffer.from(snap.turbulent.height.data.buffer, snap.turbulent.height.data.byteOffset, snap.turbulent.height.data.byteLength),
  'region-mask.u8': Buffer.from(snap.region.mask.data),
  'river.f32': Buffer.concat([le(Float32Array, snap.region.river.level), le(Float32Array, snap.region.river.center), le(Float32Array, snap.region.river.half)]),
  'route-field.u8': Buffer.from(snap.region.routeField.data),
  'colliders.bin': collidersBin(snap.colliders),
};
const fieldMeta = (f, file) => ({ file, w: f.w, h: f.h, step: f.step, x0: f.x0, y0: f.y0, min: f.min, scale: f.scale });
const pack = {
  schema: 1,
  generatedBy: 'demo/tools/bake-sim-world.mjs',
  source: snap.meta,
  coordinates: snap.coordinates,
  region: {
    half: snap.region.half,
    height: fieldMeta(snap.region.height, 'region-height.u16'),
    mask: { file: 'region-mask.u8', w: snap.region.mask.w, waterBiome: snap.region.mask.waterBiome },
    river: { file: 'river.f32', x0: snap.region.river.x0, n: snap.region.river.n },
    routeField: { file: 'route-field.u8', n: snap.region.routeField.n, step: snap.region.routeField.step, max: snap.region.routeField.max, origin: snap.region.routeField.origin },
    routes: snap.region.routes,
  },
  turbulent: {
    half: snap.turbulent.half,
    height: fieldMeta(snap.turbulent.height, 'turbulent-height.u16'),
    routes: snap.turbulent.routes,
  },
  bridges: snap.bridges,
  colliders: { file: 'colliders.bin', count: snap.colliders.length },
  places: Object.fromEntries(Object.entries(snap.places).map(([k, { name, ...rest }]) => [k, rest])),
  zones: snap.zones,
  sha256: Object.fromEntries(Object.entries(files).map(([k, v]) => [k, sha(v)])),
};

// Nomes dos lugares (texto de interface: só o cliente lê). Os de LOC vêm da própria tabela; os
// demais são os literais que `placeAt` devolve em layout.js — conferidos no código-fonte.
const layoutSrc = readFileSync(join(DEMO, 'src', 'game', 'layout.js'), 'utf8');
const FIXED_PLACES = {
  turbulenta: 'Região Turbulenta', estradaLonga: 'Estrada Longa', rioLargo: 'Rio Largo',
  camposNorte: 'Campos do Norte', camposVale: 'Campos do Vale', colinasOeste: 'Colinas do Oeste',
  encostasSerra: 'Encostas da Serra',
};
for (const name of Object.values(FIXED_PLACES)) {
  if (!layoutSrc.includes(`'${name}'`)) throw new Error(`bake: o lugar "${name}" não aparece mais em layout.js`);
}
const placeNames = { ...Object.fromEntries(Object.entries(snap.places).map(([k, l]) => [k, { name: l.name }])), ...Object.fromEntries(Object.entries(FIXED_PLACES).map(([k, n]) => [k, { name: n }])) };

const outputs = {
  ...Object.fromEntries(Object.entries(files).map(([k, v]) => [join(OUT_SIM, k), v])),
  [join(OUT_SIM, 'simpack.json')]: Buffer.from(JSON.stringify(pack, null, 1) + '\n'),
  [join(OUT_FIXTURES, 'world-parity.json')]: Buffer.from(JSON.stringify(fix) + '\n'),
  [join(OUT_TEXT, 'places.json')]: Buffer.from(JSON.stringify(placeNames, null, 2) + '\n'),
};

let changed = 0;
for (const [path, buf] of Object.entries(outputs)) {
  const old = existsSync(path) ? readFileSync(path) : null;
  if (old && old.equals(buf)) continue;
  changed++;
  if (check) { console.error(`desatualizado: ${relative(ROOT, path)}`); continue; }
  mkdirSync(dirname(path), { recursive: true });
  writeFileSync(path, buf);
  console.log(`gravado  ${relative(ROOT, path)}  (${(buf.length / 1024).toFixed(0)} KB)`);
}
console.log(`colisores: ${snap.colliders.length} · pontes: ${snap.bridges.length} · amostras: ${fix.terrain.length} terreno, ${fix.collision.length} colisão, ${fix.movement.length} caminhantes`);
if (check && changed) {
  console.error(`${changed} arquivo(s) diferem da demo. Rode: node demo/tools/bake-sim-world.mjs`);
  process.exit(1);
}
console.log(check ? 'pacote de simulação em dia com a demo' : `${changed} arquivo(s) atualizado(s)`);
