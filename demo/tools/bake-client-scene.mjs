// Pacote visual do cliente em C++ (cpp/assets/client): a cena da demo montada pelo próprio código dela,
// exportada pronta para desenhar.
//
//   node demo/tools/bake-client-scene.mjs           → grava os arquivos
//   node demo/tools/bake-client-scene.mjs --check   → só confere; sai com código 1 se algo mudou
//
// Em vez de portar para C++ a montagem visual do mundo (relevo com dez camadas de solo, construções,
// ~22 mil árvores do kit de natureza com LOD por instância, impostores, estradas, rio, acréscimos e
// objetos de jogo), este script roda essa montagem num Chromium sem janela (WebGL por software, para
// as texturas procedurais e os impostores saírem idênticos) e grava o resultado:
//
//   scene.json   — tabelas: geometrias, materiais (com os recursos de `patchMaterial`), texturas,
//                  blocos de relevo, malhas estáticas, instâncias, campos de LOD e modelos dinâmicos
//   scene.bin    — os arrays (atributos, índices, matrizes, pixels) alinhados a 16 bytes
//   tex/*.webp   — uma imagem por textura (RGBA cru vira WebP de qualidade 95 com alfa sem perdas;
//                  imagens embutidas mantêm os bytes originais)
//
// E, em cpp/tests/parity/fixtures/anim-parity.json, poses de referência dos animadores da demo
// (roteiros de entrada rodados em characters.js) para os testes de paridade da animação.
//
// A demo não muda: um plugin do esbuild só acrescenta, neste pacote, onde `patchMaterial` guarda os
// recursos e uniforms de cada material e onde o GLTFLoader guarda os bytes de cada imagem.
//
// Requer: os arquivos do Git LFS de demo/assets, `npm ci` em demo/tools e o Playwright com Chromium
// (local ou global: `npm i -g playwright && npx playwright install chromium`).
import { createHash } from 'node:crypto';
import { execSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { build } from 'esbuild';
import { MeshoptEncoder } from 'meshoptimizer';
import sharp from 'sharp';

const TOOLS = dirname(fileURLToPath(import.meta.url));
const DEMO = dirname(TOOLS);
const ROOT = dirname(DEMO);
const OUT = join(ROOT, 'cpp', 'assets', 'client');
const check = process.argv.includes('--check');
const quiet = process.argv.includes('--quiet');
// Texturas cruas: WebP de qualidade 95 com alfa sem perdas (o alfa de várias texturas guarda altura e
// rugosidade, não opacidade). `--lossless` grava tudo sem perdas (~3× maior).
const lossless = process.argv.includes('--lossless');

// ---------------------------------------------------------------- Playwright (local ou global)
function loadPlaywright() {
  const require = createRequire(import.meta.url);
  try { return require('playwright'); } catch { /* tenta o global */ }
  const root = execSync('npm root -g').toString().trim();
  return require(join(root, 'playwright'));
}

// ---------------------------------------------------------------- acréscimos só no pacote
const PATCHES = [
  {
    file: /[\\/]src[\\/]engine[\\/]materials\.js$/,
    apply: (s) => replaceOnce(s, 'export function patchMaterial(m, feats, uniforms = {}) {',
      'export function patchMaterial(m, feats, uniforms = {}) {\n  m.userData.__feats = feats; m.userData.__uniforms = uniforms;'),
  },
  {
    file: /[\\/]vendor[\\/]three[\\/]addons[\\/]loaders[\\/]GLTFLoader\.js$/,
    apply: (s) => replaceOnce(replaceOnce(s,
      'const blob = new Blob( [ bufferView ], { type: sourceDef.mimeType } );',
      'parser.__bytes = parser.__bytes || {}; parser.__bytes[ sourceIndex ] = new Uint8Array( bufferView );\n\t\t\t\tconst blob = new Blob( [ bufferView ], { type: sourceDef.mimeType } );'),
      'texture.userData.mimeType = sourceDef.mimeType || getImageURIMimeType( sourceDef.uri );',
      'texture.userData.mimeType = sourceDef.mimeType || getImageURIMimeType( sourceDef.uri );\n\t\t\tif ( parser.__bytes && parser.__bytes[ sourceIndex ] ) { texture.source.__bytes = parser.__bytes[ sourceIndex ]; texture.source.__mime = sourceDef.mimeType; }'),
  },
];
function replaceOnce(src, needle, replacement) {
  const n = src.split(needle).length - 1;
  if (n !== 1) throw new Error(`bake: esperado exatamente um "${needle.slice(0, 60)}…" (achados: ${n}); a demo mudou`);
  return src.replace(needle, replacement);
}
const patchPlugin = {
  name: 'bake-client-patches',
  setup(b) {
    for (const p of PATCHES) b.onLoad({ filter: p.file }, (args) => ({ contents: p.apply(readFileSync(args.path, 'utf8')), loader: 'js' }));
  },
};

// ---------------------------------------------------------------- WebP a partir de RGBA cru (sem pré-multiplicar)
async function webp(rgba, w, h) {
  const img = sharp(rgba, { raw: { width: w, height: h, channels: 4 } });
  return lossless ? img.webp({ lossless: true, effort: 4 }).toBuffer() : img.webp({ quality: 95, alphaQuality: 100, effort: 4 }).toBuffer();
}

// ---------------------------------------------------------------- empacotar e rodar no Chromium
const bundle = join(TOOLS, 'bake', '.client-scene.bundle.js');
const page = join(TOOLS, 'bake', '.client-scene.html');
await build({
  absWorkingDir: DEMO,
  entryPoints: ['tools/bake/client-scene.entry.js'],
  bundle: true, format: 'iife', globalName: '__clientBake', platform: 'browser', target: 'es2022',
  alias: { three: './vendor/three/three.module.js' },
  loader: { '.glb': 'binary', '.bin': 'binary', '.json': 'json', '.webp': 'dataurl' },
  outfile: bundle, plugins: [patchPlugin], logLevel: 'warning',
});
writeFileSync(page, '<!doctype html><meta charset="utf-8"><body><script src=".client-scene.bundle.js"></script></body>');

const received = [];
let json = null;
const { chromium } = loadPlaywright();
const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
try {
  const p = await browser.newPage();
  p.on('console', (m) => { if (!quiet || m.type() === 'error') console.log(`  [página] ${m.text()}`); });
  p.on('pageerror', (e) => console.error(`  [página] ${e.message}`));
  await p.exposeFunction('__bakeLog', (m) => { if (!quiet) console.log(`  ${m}`); });
  await p.exposeFunction('__bakeChunk', (b64) => { received.push(Buffer.from(b64, 'base64')); });
  await p.goto(pathToFileURL(page).href);
  const t0 = Date.now();
  json = await p.evaluate(async () => {
    const { json, chunks } = await window.__clientBake.bake((m) => window.__bakeLog(m));
    // blocos em base64, em lotes de ~4 MB
    let batch = [], size = 0;
    const flush = async () => {
      if (!batch.length) return;
      const all = new Uint8Array(size);
      let o = 0; for (const b of batch) { all.set(b, o); o += b.length; }
      let s = '';
      for (let i = 0; i < all.length; i += 0x8000) s += String.fromCharCode.apply(null, all.subarray(i, i + 0x8000));
      await window.__bakeChunk(btoa(s));
      batch = []; size = 0;
    };
    for (const c of chunks) { batch.push(c); size += c.length; if (size > 4 << 20) await flush(); }
    await flush();
    return json;
  });
  if (!quiet) console.log(`cena exportada em ${Date.now() - t0} ms`);
} finally {
  await browser.close();
  rmSync(bundle, { force: true });
  rmSync(page, { force: true });
}

const bin = Buffer.concat(received);
if (bin.length !== json.binSize) throw new Error(`binário incompleto: ${bin.length} de ${json.binSize} bytes`);

// ---------------------------------------------------------------- texturas como arquivos
const files = new Map();   // nome → bytes
const sources = json.sources.map((s) => {
  if (s.kind === 'encoded') {
    const bytes = bin.subarray(s.bytes, s.bytes + s.length);
    const ext = s.mime === 'image/png' ? 'png' : s.mime === 'image/jpeg' ? 'jpg' : 'webp';
    const name = `tex/${createHash('sha256').update(bytes).digest('hex').slice(0, 16)}.${ext}`;
    files.set(name, Buffer.from(bytes));
    return { w: s.w, h: s.h, files: [name] };
  }
  return { w: s.w, h: s.h, raw: s.layers };
});
for (const s of sources) {
  if (!s.raw) continue;
  const per = s.w * s.h * 4;
  s.files = [];
  for (const off of s.raw) {
    const data = await webp(bin.subarray(off, off + per), s.w, s.h);
    const name = `tex/${createHash('sha256').update(data).digest('hex').slice(0, 16)}.webp`;
    files.set(name, data);
    s.files.push(name);
  }
  delete s.raw;
}
const textures = [];
for (const t of json.textures) {
  const mipLevels = [];
  for (const m of t.mipLevels) {
    const data = await webp(bin.subarray(m.offset, m.offset + m.w * m.h * 4), m.w, m.h);
    const name = `tex/${createHash('sha256').update(data).digest('hex').slice(0, 16)}.webp`;
    files.set(name, data);
    mipLevels.push({ w: m.w, h: m.h, file: name });
  }
  textures.push({ ...t, mipLevels });
}

// O binário final só leva geometria, instâncias e pesos do relevo (os pixels foram para os arquivos
// de textura), cada fluxo comprimido com o codec do meshoptimizer (sem perdas; o C++ decodifica com
// meshopt_decodeVertexBuffer / meshopt_decodeIndexBuffer).
await MeshoptEncoder.ready;
const parts = [];
let size = 0;
const put = (bytes) => {
  const pad = (16 - (size % 16)) % 16;
  if (pad) { parts.push(Buffer.alloc(pad)); size += pad; }
  const at = size;
  parts.push(Buffer.from(bytes));
  size += bytes.length;
  return at;
};
// fluxo de vértices: `count` elementos de `stride` bytes (o codec pede múltiplo de 4, até 256)
const vstream = (off, count, stride) => {
  const raw = new Uint8Array(bin.buffer, bin.byteOffset + off, count * stride);
  if (stride % 4 === 0 && stride <= 256 && count > 0) {
    const enc = MeshoptEncoder.encodeVertexBuffer(raw, count, stride);
    return { offset: put(enc), bytes: enc.length, count, stride, codec: 'meshopt-v' };
  }
  return { offset: put(raw), bytes: raw.length, count, stride, codec: 'raw' };
};
const istream = (off, count) => {
  const raw = new Uint8Array(bin.buffer, bin.byteOffset + off, count * 4);
  if (count % 3 === 0 && count > 0) {
    const enc = MeshoptEncoder.encodeIndexBuffer(raw, count, 4);
    return { offset: put(enc), bytes: enc.length, count, stride: 4, codec: 'meshopt-i' };
  }
  return { offset: put(raw), bytes: raw.length, count, stride: 4, codec: 'raw' };
};
const geometries = json.geometries.map((g) => ({
  ...g,
  attributes: Object.fromEntries(Object.entries(g.attributes).map(([k, a]) => {
    const stride = a.size * (a.type === 'u8n' ? 1 : a.type === 'u16' ? 2 : 4);
    return [k, { type: a.type, size: a.size, ...vstream(a.offset, g.count, stride) }];
  })),
  index: g.index ? istream(g.index.offset, g.index.count) : null,
}));
const instanced = json.instanced.map((i) => ({
  ...i,
  matrices: vstream(i.matrices, i.count, 64),
  colors: i.colors >= 0 ? vstream(i.colors, i.count, 12) : null,
}));
const lodFields = json.lodFields.map((f) => ({ ...f, chunks: f.chunks.map((c) => ({ ...c, items: vstream(c.items, c.n, 80) })) }));
const terrain = json.terrain.map((t) => ({
  ...t,
  near: { ...t.near, splat: vstream(t.near.splat, t.near.n * t.near.n, 12) },
  far: { ...t.far, splat: vstream(t.far.splat, t.far.n * t.far.n, 12) },
}));
// personagens: pose de repouso (10 floats por nó), bindMatrix, inversas dos ossos e trilhas dos clipes
const rigs = Object.fromEntries(Object.entries(json.rigs || {}).map(([id, r]) => [id, {
  ...r,
  rest: vstream(r.rest, r.nodes.length, 40),
  meshes: r.meshes.map((m) => (m.skinned ? { ...m, bindMatrix: vstream(m.bindMatrix, 1, 64), boneInverses: vstream(m.boneInverses, m.bones.length, 64) } : m)),
  clips: Object.fromEntries(Object.entries(r.clips).map(([k, c]) => [k, {
    ...c,
    tracks: c.tracks.map((t) => ({ ...t, times: vstream(t.times, t.count, 4), values: vstream(t.values, t.count, t.size * 4) })),
  }])),
}]));
// cobertura do chão: 16 bytes por instância (ver client-scene.entry.js); por último, para não mudar os
// deslocamentos do resto do binário
const groundCover = json.groundCover ? { ...json.groundCover, items: vstream(json.groundCover.items, json.groundCover.count, 16) } : null;
const sceneBin = Buffer.concat(parts);

// Luz de ambiente do dia: o HDRI reduzido do kit (RGBE em WebP sem perdas) vai como está, com as
// cores dominantes que `calibrateDaylight` usa (kit-manifest.json → sky).
const kitSky = JSON.parse(readFileSync(join(DEMO, 'assets', 'nature-kit', 'kit-manifest.json'), 'utf8')).sky;
const envFile = `tex/${kitSky.file}`;
files.set(envFile, readFileSync(join(DEMO, 'assets', 'nature-kit', kitSky.file)));
const environment = { file: envFile, width: kitSky.width, height: kitSky.height, encoding: kitSky.encoding, colors: kitSky.colors };

const scene = {
  schema: json.schema,
  generatedBy: 'demo/tools/bake-client-scene.mjs',
  lodBands: json.lodBands,
  sources, textures, materials: json.materials, geometries,
  terrain, static: json.static, instanced, lodFields,
  dynamic: json.dynamic, templates: json.templates, environment, rigs, clipMeta: json.clipMeta, groundCover,
  bin: { file: 'scene.bin', bytes: sceneBin.length, sha256: createHash('sha256').update(sceneBin).digest('hex') },
};
files.set('scene.bin', sceneBin);
files.set('scene.json', Buffer.from(JSON.stringify(scene) + '\n'));
// paridade da animação (tests/client/AnimTests.cpp): fica com as outras fixtures de paridade
const FIXTURES = join(ROOT, 'cpp', 'tests', 'parity', 'fixtures');
const animParity = Buffer.from(JSON.stringify(json.animParity) + '\n');

// ---------------------------------------------------------------- gravar ou conferir
const summary = `${geometries.length} geometrias, ${json.materials.length} materiais, ${textures.length} texturas `
  + `(${sources.length} imagens), ${json.terrain.length} blocos de relevo, ${json.static.length} malhas, `
  + `${instanced.length} instanciadas, ${lodFields.length} campos de LOD, `
  + `${groundCover ? groundCover.count : 0} instâncias de cobertura do chão; scene.bin ${(sceneBin.length / 1048576).toFixed(1)} MB`;
if (check) {
  const bad = [];
  for (const [name, data] of files) {
    const p = join(OUT, name);
    if (!existsSync(p) || !readFileSync(p).equals(data)) bad.push(name);
  }
  const ap = join(FIXTURES, 'anim-parity.json');
  if (!existsSync(ap) || !readFileSync(ap).equals(animParity)) bad.push('../../tests/parity/fixtures/anim-parity.json');
  if (bad.length) {
    console.error(`cpp/assets/client desatualizado (${bad.length} arquivos): ${bad.slice(0, 8).join(', ')}…\nRode: node demo/tools/bake-client-scene.mjs`);
    process.exit(1);
  }
  console.log(`cpp/assets/client em dia: ${summary}`);
} else {
  rmSync(join(OUT, 'tex'), { recursive: true, force: true });
  mkdirSync(join(OUT, 'tex'), { recursive: true });
  for (const [name, data] of files) writeFileSync(join(OUT, name), data);
  writeFileSync(join(FIXTURES, 'anim-parity.json'), animParity);
  const texBytes = readdirSync(join(OUT, 'tex')).reduce((s, f) => s + readFileSync(join(OUT, 'tex', f)).length, 0);
  console.log(`gravado em cpp/assets/client: ${summary}; texturas ${(texBytes / 1048576).toFixed(1)} MB`);
}
