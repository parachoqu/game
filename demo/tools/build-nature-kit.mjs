// Prepara o kit de natureza da demo a partir de "Ultimate Nature – Starter"
// (Innerverse Interactive), entregue como .unitypackage em `modelos 3d animados`.
//
//   node tools/build-nature-kit.mjs            → demo/assets/nature-kit/
//   node tools/build-nature-kit.mjs --no-sky   → pula o envmap do HDRI (que é o passo lento)
//
// Saídas:
//   nature-kit.glb      protótipos com três níveis de detalhe por tipo, meshopt
//   kit-manifest.json   tipos, variantes, níveis, dimensões medidas, materiais e cores
//   tex/*.webp          paleta, folhagens, grama, flor e as duas camadas de terreno
//   sky-env.webp        envmap equirretangular RGBE reduzido do HDRI de 250 MB
//   kit-report.json     tamanho de entrada, saída, redução e hash de cada peça
//
// O .unitypackage é tratado como fonte somente leitura: nada é escrito em
// `modelos 3d animados/`. Os arquivos intermediários ficam num diretório temporário do sistema.
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { createReadStream } from 'node:fs';
import { mkdirSync, writeFileSync, readFileSync, existsSync, statSync, rmSync } from 'node:fs';
import { createGunzip } from 'node:zlib';
import { tmpdir } from 'node:os';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import sharp from 'sharp';
import { Document, NodeIO, PropertyType } from '@gltf-transform/core';
import { KHRONOS_EXTENSIONS, EXTMeshoptCompression } from '@gltf-transform/extensions';
import { dedup, prune, weld, quantize, reorder, meshopt, mergeDocuments } from '@gltf-transform/functions';
import { MeshoptEncoder, MeshoptDecoder, MeshoptSimplifier } from 'meshoptimizer';
import { parseUnityAssets } from './parse-unity-assets.mjs';

const TOOLS = dirname(fileURLToPath(import.meta.url));
const DEMO = dirname(TOOLS);
const ROOT = dirname(DEMO);
const PKG = join(ROOT, 'modelos 3d animados', 'Ultimate Nature Starter.unitypackage');
const OUT = join(DEMO, 'assets', 'nature-kit');
const TMP = join(tmpdir(), 'projeto-game-nature-kit');
const FBX2GLTF = join(TOOLS, 'node_modules/fbx2gltf/bin',
  process.platform === 'win32' ? 'Windows_NT' : process.platform === 'darwin' ? 'Darwin' : 'Linux',
  process.platform === 'win32' ? 'FBX2glTF.exe' : 'FBX2glTF');

const KIT_SCHEMA = '1.0.0';
const FBX_SCALE = 1;
const fail = (m) => { console.error(`build-nature-kit: ${m}`); process.exit(1); };
const sha256 = (b) => createHash('sha256').update(b).digest('hex');
const round = (v, n = 3) => Math.round(v * 10 ** n) / 10 ** n;
const KB = (n) => `${(n / 1024).toFixed(1)} KB`;

// ---------------------------------------------------------------- catálogo
// Mapeamento arquivo do pacote → tipo lógico da demo.
// `pine` é conífera grande (UNS_Spruce_01).
// `broad` é conífera pequena (UNS_Spruce_02) mantendo alias do jogo para compatibilidade.
// O pacote NÃO possui árvore folhosa verdadeira; broad é conífera pequena estilizada.
// `budget` é o orçamento indicativo de triângulos para os níveis (LOD0 perto, LOD1/2 médio, LOD3 longe).
const CATALOG = [
  { type: 'pine', files: ['UNS_Spruce_01'], lodPick: [0, 2, 3], foliage: 'Branch' },
  { type: 'broad', files: ['UNS_Spruce_02'], lodPick: [0, 2, 3], foliage: 'Branch', tint: '#8fae63', role: 'small_conifer' },
  { type: 'dead', files: ['UNS_Spruce_01'], lodPick: [0, 1, 3], dropFoliage: true },
  { type: 'bush', files: ['UNS_Bush'], lodPick: [0, 1, 3], foliage: 'Leaves' },
  { type: 'grass', files: ['UNS_Grass'], lodPick: [0, 1, 3], foliage: 'Grass' },
  { type: 'flower', files: ['UNS_Flower'], lodPick: [0, 1, 3], foliage: 'Flower' },
  { type: 'mushroom', files: ['UNS_Mushroom_Patch'], lodPick: [0, 1, 3] },
  { type: 'rock', files: ['UNS_Standard_Rock_01', 'UNS_Standard_Rock_02', 'UNS_Standard_Rock_03', 'UNS_Standard_Rock_04', 'UNS_Standard_Rock_05'], lodPick: [0, 1, 3] },
  { type: 'cliff', files: ['UNS_Rock_Cliff_01', 'UNS_Rock_Cliff_02', 'UNS_Rock_Cliff_03', 'UNS_Rock_Cliff_04', 'UNS_Rock_Cliff_05'], lodPick: [0, 1, 3] },
  { type: 'pebble', files: ['UNS_Tiny_Rock_01', 'UNS_Tiny_Rock_02', 'UNS_Tiny_Rock_03', 'UNS_Tiny_Rock_04', 'UNS_Tiny_Rock_05'], lodPick: [0, 1, 3] },
  { type: 'log', files: ['UNS_Log'], lodPick: [0, 1, 3] },
  { type: 'stump', files: ['UNS_Stump'], lodPick: [0, 1, 3] },
  { type: 'branch', files: ['UNS_Branch'], lodPick: [0, 1, 3] },
  { type: 'mountain', files: ['UNS_Mountain'], lodPick: [0, 1, 3] },
];

// Identidades de materiais autorais do kit extraídas diretamente dos .mat Unity.
const MATERIALS = {
  palette: { match: /palette/i, texture: 'palette', color: '#ffffff', alphaTest: 0, roughness: 0.88, doubleSided: false },
  flowerLeaf: { match: /flower\s*leaf/i, texture: 'flowerLeaf', color: '#709e38', alphaTest: 0.5, roughness: 1.0, doubleSided: true, foliage: true },
  flower: { match: /flower/i, texture: 'flower', color: '#8c6bd9', alphaTest: 0.5, roughness: 1.0, doubleSided: true, foliage: true },
  branch: { match: /branch/i, texture: 'branch', color: '#526e38', alphaTest: 0.5, roughness: 1.0, doubleSided: true, foliage: true },
  leaves: { match: /leaves/i, texture: 'leaves', color: '#739438', alphaTest: 0.5, roughness: 1.0, doubleSided: true, foliage: true },
  grass: { match: /grass/i, texture: 'grass', color: '#849e33', alphaTest: 0.301, roughness: 1.0, doubleSided: true, foliage: true },
  water: { match: /water/i, texture: null, color: '#386e85', alphaTest: 0, roughness: 0.18, doubleSided: false },
};
const MATERIAL_ORDER = ['flowerLeaf', 'flower', 'branch', 'leaves', 'grass', 'water', 'palette'];
function matKey(name) {
  for (const k of MATERIAL_ORDER) if (MATERIALS[k].match.test(name || '')) return k;
  return 'palette';
}

function hexToRgb(hex) {
  const c = parseInt(hex.replace('#', ''), 16);
  return [(c >> 16 & 255) / 255, (c >> 8 & 255) / 255, (c & 255) / 255];
}

// texturas do pacote → arquivo WebP do kit
const TEXTURES = {
  'UNS_Color_Palette.png': { id: 'palette', size: 512, alpha: false, quality: 92 },
  'UNS_Spruce_Tree_Branch.png': { id: 'branch', size: 512, alpha: true, quality: 90 },
  'UNS_Bush_Leaves.png': { id: 'leaves', size: 512, alpha: true, quality: 90 },
  'UNS_Grass.png': { id: 'grass', size: 512, alpha: true, quality: 90 },
  'UNS_Flower.png': { id: 'flower', size: 256, alpha: true, quality: 92 },
  'UNS_Flower_Leaf.png': { id: 'flowerLeaf', size: 256, alpha: true, quality: 92 },
  'UNS_Terrain_Grass.png': { id: 'terrainGrass', size: 512, alpha: false, quality: 88 },
  'UNS_Terrain_Dirt.png': { id: 'terrainDirt', size: 512, alpha: false, quality: 88 },
};

// ---------------------------------------------------------------- leitura do .unitypackage
async function readUnityPackage(file, wanted) {
  if (!existsSync(file)) fail(`pacote não encontrado: ${file}`);
  const chunks = [];
  await new Promise((resolve, reject) => {
    createReadStream(file).pipe(createGunzip())
      .on('data', (c) => chunks.push(c))
      .on('end', resolve).on('error', reject);
  });
  const tar = Buffer.concat(chunks);
  const entries = new Map();
  let p = 0;
  while (p + 512 <= tar.length) {
    const name = tar.toString('utf8', p, p + 100).replace(/\0.*$/, '');
    if (!name) { p += 512; continue; }
    const sizeField = tar.toString('utf8', p + 124, p + 136).replace(/\0.*$/, '').trim();
    const size = parseInt(sizeField, 8) || 0;
    const type = tar.toString('utf8', p + 156, p + 157);
    p += 512;
    if (type === '0' || type === '\0') {
      const body = tar.subarray(p, p + size);
      const parts = name.split('/');
      if (parts.length >= 2) {
        const guid = parts[0] === '.' ? parts[1] : parts[0];
        const kind = parts[parts.length - 1];
        const e = entries.get(guid) || {};
        if (kind === 'pathname') e.pathname = body.toString('utf8').split('\n')[0].trim();
        else if (kind === 'asset') e.asset = Buffer.from(body);
        entries.set(guid, e);
      }
    }
    p += Math.ceil(size / 512) * 512;
  }
  const byName = new Map();
  for (const e of entries.values()) {
    if (!e.pathname || !e.asset) continue;
    const base = e.pathname.split('/').pop();
    if (!wanted || wanted(base, e.pathname)) byName.set(base, e.asset);
  }
  return byName;
}

// ---------------------------------------------------------------- HDRI → envmap
function hdrToEnvmap(buf, outW = 256, outH = 128) {
  let p = 0;
  const line = () => { const s = p; while (buf[p] !== 0x0a) p++; return buf.toString('latin1', s, p++); };
  if (!line().startsWith('#?')) fail('HDRI: assinatura Radiance ausente');
  let format = '';
  for (let l = line(); l.length; l = line()) if (l.startsWith('FORMAT=')) format = l.split('=')[1];
  if (format !== '32-bit_rle_rgbe') fail(`HDRI: formato não suportado: ${format}`);
  const [resY, resX] = line().split(/\s+/).filter(Boolean);
  const srcH = parseInt(resY.replace('-Y', ''), 10);
  const srcW = parseInt(resX.replace('+X', ''), 10);
  if (!srcW || !srcH) fail(`HDRI: resolução inválida (${srcW}×${srcH})`);

  const accR = new Float64Array(outW * outH);
  const accG = new Float64Array(outW * outH);
  const accB = new Float64Array(outW * outH);
  const counts = new Uint32Array(outW * outH);
  const scan = new Uint8Array(srcW * 4);
  let peak = 0;
  const zenithRgb = [0, 0, 0], zenithN = [0];
  const horizonRgb = [0, 0, 0], horizonN = [0];
  const groundRgb = [0, 0, 0], groundN = [0];

  for (let y = 0; y < srcH; y++) {
    if (buf[p] !== 2 || buf[p + 1] !== 2) fail(`HDRI: scanline ${y} sem cabeçalho RLE adaptativo`);
    const w = (buf[p + 2] << 8) | buf[p + 3];
    if (w !== srcW) fail(`HDRI: largura da scanline inconsistente (${w} !== ${srcW})`);
    p += 4;
    for (let c = 0; c < 4; c++) {
      let x = 0;
      while (x < srcW) {
        const code = buf[p++];
        if (code > 128) {
          const run = code - 128, val = buf[p++];
          for (let i = 0; i < run; i++) scan[(x + i) * 4 + c] = val;
          x += run;
        } else {
          for (let i = 0; i < code; i++) scan[(x + i) * 4 + c] = buf[p++];
          x += code;
        }
      }
    }

    const outY = Math.min(outH - 1, Math.floor(y / srcH * outH));
    const isZenith = y < srcH * 0.12;
    const isHorizon = y >= srcH * 0.44 && y < srcH * 0.56;
    const isGround = y >= srcH * 0.75;

    for (let x = 0; x < srcW; x++) {
      const idx = x * 4;
      const exp = scan[idx + 3];
      if (exp === 0) continue;
      const scale = 2 ** (exp - 128 - 8);
      const r = scan[idx] * scale, g = scan[idx + 1] * scale, b = scan[idx + 2] * scale;
      const lum = r * 0.2126 + g * 0.7152 + b * 0.0722;
      if (lum > peak) peak = lum;
      const outX = Math.min(outW - 1, Math.floor(x / srcW * outW));
      const k = outY * outW + outX;
      accR[k] += r; accG[k] += g; accB[k] += b; counts[k]++;

      if (isZenith) { zenithRgb[0] += r; zenithRgb[1] += g; zenithRgb[2] += b; zenithN[0]++; }
      else if (isHorizon) { horizonRgb[0] += r; horizonRgb[1] += g; horizonRgb[2] += b; horizonN[0]++; }
      else if (isGround) { groundRgb[0] += r; groundRgb[1] += g; groundRgb[2] += b; groundN[0]++; }
    }
  }

  const rgbe = new Uint8Array(outW * outH * 4);
  for (let k = 0; k < outW * outH; k++) {
    const n = counts[k] || 1;
    const r = accR[k] / n, g = accG[k] / n, b = accB[k] / n;
    const maxVal = Math.max(r, g, b);
    if (maxVal < 1e-32) { rgbe.fill(0, k * 4, k * 4 + 4); continue; }
    const exp = Math.min(255, Math.max(0, Math.floor(Math.log2(maxVal)) + 1 + 128));
    const s = 256 / 2 ** (exp - 128);
    rgbe[k * 4] = Math.min(255, Math.round(r * s));
    rgbe[k * 4 + 1] = Math.min(255, Math.round(g * s));
    rgbe[k * 4 + 2] = Math.min(255, Math.round(b * s));
    rgbe[k * 4 + 3] = exp;
  }

  const avgHex = (rgb, n) => {
    const s = n[0] || 1;
    const toHex = (v) => Math.min(255, Math.round(Math.pow(Math.min(1, v / s), 1 / 2.2) * 255)).toString(16).padStart(2, '0');
    return `#${toHex(rgb[0])}${toHex(rgb[1])}${toHex(rgb[2])}`;
  };

  return {
    rgbe, width: outW, height: outH, source: `${srcW}x${srcH}`, peak: round(peak, 1),
    colors: { zenith: avgHex(zenithRgb, zenithN), horizon: avgHex(horizonRgb, horizonN), ground: avgHex(groundRgb, groundN) },
  };
}

// ---------------------------------------------------------------- geometria & bounds
const triCount = (p) => (p.getIndices() ? p.getIndices().getCount() / 3 : p.getAttribute('POSITION').getCount() / 3);
const meshTris = (m) => m.listPrimitives().reduce((s, p) => s + triCount(p), 0);

function nodeWorldBox(node) {
  const m = node.getWorldMatrix();
  const min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
  const p = [0, 0, 0];
  const mesh = node.getMesh();
  if (!mesh) return null;
  for (const prim of mesh.listPrimitives()) {
    const pos = prim.getAttribute('POSITION');
    for (let k = 0; k < pos.getCount(); k++) {
      pos.getElement(k, p);
      const v = [
        m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12],
        m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
        m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14],
      ];
      for (let i = 0; i < 3; i++) { if (v[i] < min[i]) min[i] = v[i]; if (v[i] > max[i]) max[i] = v[i]; }
    }
  }
  return min[0] === Infinity ? null : { min, max };
}

function baseRadius(node, box) {
  const mesh = node.getMesh();
  const m = node.getWorldMatrix();
  const p = [0, 0, 0];
  const height = box.max[1] - box.min[1];
  const cut = box.min[1] + Math.min(0.6, Math.max(0.12, height * 0.08));
  let r = 0, any = false;
  for (const prim of mesh.listPrimitives()) {
    if (MATERIALS[matKey(prim.getMaterial()?.getName())].foliage) continue;
    any = true;
    const pos = prim.getAttribute('POSITION');
    for (let k = 0; k < pos.getCount(); k++) {
      pos.getElement(k, p);
      const y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
      if (y > cut) continue;
      const x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
      const z = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
      r = Math.max(r, Math.hypot(x, z));
    }
  }
  if (!any) return 0;
  const flat = Math.max(box.max[0] - box.min[0], box.max[2] - box.min[2]);
  if (height < 1 && flat > height * 2.5) r = Math.min(r, Math.max(0.2, height * 0.9));
  return r;
}

function bakeTransform(prim, matrix) {
  const pos = prim.getAttribute('POSITION');
  const nor = prim.getAttribute('NORMAL');
  const m = matrix;
  const p = [0, 0, 0];
  for (let k = 0; k < pos.getCount(); k++) {
    pos.getElement(k, p);
    pos.setElement(k, [
      m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12],
      m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
      m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14],
    ]);
  }
  if (nor) {
    for (let k = 0; k < nor.getCount(); k++) {
      nor.getElement(k, p);
      const v = [
        m[0] * p[0] + m[4] * p[1] + m[8] * p[2],
        m[1] * p[0] + m[5] * p[1] + m[9] * p[2],
        m[2] * p[0] + m[6] * p[1] + m[10] * p[2],
      ];
      const l = Math.hypot(v[0], v[1], v[2]) || 1;
      nor.setElement(k, [v[0] / l, v[1] / l, v[2] / l]);
    }
  }
}

// ---------------------------------------------------------------- main
async function main() {
  const skipSky = process.argv.includes('--no-sky');
  await MeshoptEncoder.ready; await MeshoptDecoder.ready; await MeshoptSimplifier.ready;
  if (!existsSync(FBX2GLTF)) fail(`FBX2glTF não encontrado em ${FBX2GLTF} (rode: cd tools && npm install)`);
  mkdirSync(OUT, { recursive: true });
  mkdirSync(join(OUT, 'tex'), { recursive: true });
  rmSync(TMP, { recursive: true, force: true });
  mkdirSync(TMP, { recursive: true });

  console.log('· lendo prefabs e materiais autorais do Unity');
  const { materials: unityMats, prefabs: unityPrefabs } = await parseUnityAssets();

  const needFbx = new Set(CATALOG.flatMap((c) => c.files).map((f) => `${f}.fbx`));
  const needPng = new Set(Object.keys(TEXTURES));

  console.log('· lendo arquivos-fonte do .unitypackage');
  const assets = await readUnityPackage(PKG, (base) =>
    needFbx.has(base) || needPng.has(base) || base === 'UNS_HDRI.hdr' || base === 'UNS_Readme.txt');
  const pkgBytes = statSync(PKG).size;
  for (const f of needFbx) if (!assets.has(f)) fail(`modelo ausente no pacote: ${f}`);
  for (const t of needPng) if (!assets.has(t)) fail(`textura ausente no pacote: ${t}`);

  // ---- texturas
  console.log('· convertendo texturas em WebP');
  const texReport = [];
  const texIds = {};
  for (const [file, cfg] of Object.entries(TEXTURES)) {
    const src = assets.get(file);
    let img = sharp(src).resize(cfg.size, cfg.size, { fit: 'fill' });
    if (!cfg.alpha) img = img.removeAlpha();
    const webp = await img.webp({ quality: cfg.quality, alphaQuality: 100, effort: 6 }).toBuffer();
    const out = join(OUT, 'tex', `${cfg.id}.webp`);
    writeFileSync(out, webp);
    texIds[cfg.id] = { file: `tex/${cfg.id}.webp`, size: cfg.size, alpha: cfg.alpha, bytes: webp.length, sha256: sha256(webp) };
    texReport.push({ label: `tex/${cfg.id}.webp`, input: src.length, bytes: webp.length, sha256: sha256(webp) });
  }

  // ---- conversão FBX -> GLTF
  console.log('· convertendo modelos FBX via FBX2glTF');
  const io = new NodeIO()
    .registerExtensions([...KHRONOS_EXTENSIONS, EXTMeshoptCompression])
    .registerDependencies({ 'meshopt.decoder': MeshoptDecoder, 'meshopt.encoder': MeshoptEncoder });
  const converted = new Map();
  for (const file of needFbx) {
    const fbxPath = join(TMP, file);
    writeFileSync(fbxPath, assets.get(file));
    const outBase = join(TMP, file.replace(/\.fbx$/, ''));
    execFileSync(FBX2GLTF, ['--binary', '--input', fbxPath, '--output', outBase], { stdio: 'ignore' });
    converted.set(file.replace(/\.fbx$/, ''), `${outBase}.glb`);
  }

  // ---- montagem do kit com identidades imutáveis de material
  console.log('· montando os protótipos e preservando materiais autorais');
  const kit = new Document();
  const scene = kit.createScene('NATURE_KIT');
  const sources = new Map();
  const inputBytes = { fbx: 0 };

  for (const [fileBase, glbPath] of converted) {
    inputBytes.fbx += assets.get(`${fileBase}.fbx`).length;
    const before = new Set(kit.getRoot().listNodes());
    const doc = await io.read(glbPath);
    mergeDocuments(kit, doc);

    const lods = [];
    for (const node of kit.getRoot().listNodes()) {
      if (before.has(node) || !node.getMesh()) continue;
      const m = /_LOD(\d)$/.exec(node.getName() || '');
      if (m) lods[+m[1]] = node;
    }
    // Caso especial: modelos sem LOD explícito no nome usam primeiro nó novo com malha
    if (!lods.length) {
      for (const node of kit.getRoot().listNodes()) {
        if (!before.has(node) && node.getMesh()) { lods[0] = node; break; }
      }
    }
    sources.set(fileBase, { lods });
  }

  // Criação dos materiais glTF com parâmetros autorais e extras explícitos
  const kitMats = new Map();
  for (const [key, cfg] of Object.entries(MATERIALS)) {
    const m = kit.createMaterial(key);
    m.setName(key);
    m.setRoughnessFactor(cfg.roughness ?? 1.0);
    m.setMetallicFactor(0.0);
    m.setDoubleSided(!!cfg.doubleSided);
    if (cfg.foliage) {
      m.setAlphaMode('MASK');
      m.setAlphaCutoff(cfg.alphaTest ?? 0.5);
    } else {
      m.setAlphaMode('OPAQUE');
    }
    if (cfg.color) {
      const rgb = hexToRgb(cfg.color);
      m.setBaseColorFactor([rgb[0], rgb[1], rgb[2], 1.0]);
    }
    m.setExtras({ matKey: key, identity: key });
    kitMats.set(key, m);
  }

  const materialOf = (key) => kitMats.get(key) || kitMats.get('palette');

  const types = {};
  const keepNodes = [];

  for (const entry of CATALOG) {
    const variants = [];
    for (const fileBase of entry.files) {
      const src = sources.get(fileBase);
      if (!src || !src.lods[0]) fail(`${fileBase}: LOD0 não encontrado depois da conversão`);
      const lods = src.lods;

      const box0 = nodeWorldBox(lods[0]);
      const h0 = box0.max[1] - box0.min[1];
      const scaled = {
        height: round(h0 * FBX_SCALE),
        radius: round(Math.max(box0.max[0] - box0.min[0], box0.max[2] - box0.min[2]) * FBX_SCALE / 2),
        base: round(box0.min[1] * FBX_SCALE),
        collider: round(baseRadius(lods[0], box0) * FBX_SCALE),
      };

      // Recupera collider autoral do prefab Unity se houver
      const prefabInfo = unityPrefabs[fileBase];
      const authoringCollider = prefabInfo?.capsule || (prefabInfo?.meshColliders ? { type: 'MeshCollider', count: prefabInfo.meshColliders } : null);

      const levels = [];
      for (let lv = 0; lv < 3; lv++) {
        const pick = entry.lodPick ? entry.lodPick[lv] : lv;
        const actualPick = lods[pick] ? pick : (lods[lods.length - 1] ? lods.length - 1 : 0);
        const sourceNode = lods[actualPick];

        const sourceBox = nodeWorldBox(sourceNode);
        const sourceH = sourceBox.max[1] - sourceBox.min[1];

        // Escala normalizadora para garantir diferença de altura estritamente < 2%
        const heightScale = Math.abs(sourceH - h0) / (h0 || 1) > 0.015 ? (h0 / sourceH) : 1.0;
        const m = [...sourceNode.getWorldMatrix()];
        for (let i = 0; i < 16; i++) {
          if (i % 4 !== 3) m[i] *= FBX_SCALE;
          if (i % 4 === 1) m[i] *= heightScale; // ajusta Y para bater com LOD0
        }

        const mesh = kit.createMesh(`${entry.type}_${variants.length}_L${lv}`);
        for (const prim of sourceNode.getMesh().listPrimitives()) {
          const key = matKey(prim.getMaterial()?.getName());
          if (entry.dropFoliage && MATERIALS[key].foliage) continue;
          
          const copy = prim.clone();
          bakeTransform(copy, m);
          copy.setMaterial(materialOf(key));
          copy.setExtras({ matKey: key, type: entry.type, level: lv });
          mesh.addPrimitive(copy);
        }

        if (!mesh.listPrimitives().length) { mesh.dispose(); continue; }

        const node = kit.createNode(`${entry.type}_${variants.length}_L${lv}`).setMesh(mesh);
        scene.addChild(node);
        keepNodes.push(node);

        const tris = meshTris(mesh);
        const finalBox = nodeWorldBox(node);
        const finalHeight = round(finalBox.max[1] - finalBox.min[1]);
        const heightDiffPct = round(Math.abs(finalHeight - scaled.height) / scaled.height * 100, 2);

        if (heightDiffPct > 2.0) {
          fail(`Diferença de altura entre LODs excedeu 2%: ${entry.type} L${lv} (${finalHeight}m vs ${scaled.height}m = ${heightDiffPct}%)`);
        }

        levels.push({
          level: lv,
          node: node.getName(),
          triangles: tris,
          from_lod: actualPick,
          height_m: finalHeight,
          height_diff_pct: heightDiffPct,
        });
      }

      variants.push({
        source: fileBase,
        ...scaled,
        authoring_collider: authoringCollider,
        lod_screen_relative_heights: prefabInfo?.lods?.screenRelativeHeights || null,
        levels,
      });
    }

    types[entry.type] = {
      variants: variants.length,
      foliage: entry.foliage || null,
      tint: entry.tint || null,
      role: entry.role || entry.type,
      items: variants,
    };

    const t0 = variants[0];
    console.log(`  ${entry.type.padEnd(9)} ${variants.length} variante(s)  altura ${t0.height}m  níveis: ${t0.levels.map((l) => `${l.triangles}t (${l.from_lod})`).join(' / ')}`);
  }

  // descarta cenas, nós e materiais obsoletos
  const keep = new Set(keepNodes);
  for (const s2 of kit.getRoot().listScenes()) if (s2 !== scene) s2.dispose();
  for (const n of kit.getRoot().listNodes()) if (!keep.has(n)) n.dispose();
  for (const m of kit.getRoot().listMaterials()) if (![...kitMats.values()].includes(m)) m.dispose();
  kit.getRoot().setDefaultScene(scene);

  // Limpa atributos não utilizados preservando UV0 essencial
  for (const prim of kit.getRoot().listMeshes().flatMap((m) => m.listPrimitives())) {
    for (const sem of prim.listSemantics()) {
      if (!/^(POSITION|NORMAL|TEXCOORD_0)$/.test(sem)) prim.setAttribute(sem, null);
    }
  }
  for (const tex of kit.getRoot().listTextures()) tex.dispose();

  const buffer = kit.getRoot().listBuffers()[0] || kit.createBuffer();
  for (const a of kit.getRoot().listAccessors()) a.setBuffer(buffer);
  for (const b of kit.getRoot().listBuffers()) if (b !== buffer) b.dispose();

  // Executa transformação com dedup SELETIVO (nunca fundir materiais!)
  console.log('· executando otimização com meshopt (preservando materiais)');
  await kit.transform(
    dedup({ propertyTypes: [PropertyType.ACCESSOR, PropertyType.MESH] }),
    prune({ keepAttributes: true, keepLeaves: false }),
    weld(),
    reorder({ encoder: MeshoptEncoder, target: 'performance' }),
    quantize({ pattern: /^(POSITION|NORMAL|TEXCOORD|COLOR)(_\d+)?$/ }),
    meshopt({ encoder: MeshoptEncoder, level: 'high' }),
  );

  const kitBin = Buffer.from(await io.writeBinary(kit));
  writeFileSync(join(OUT, 'nature-kit.glb'), kitBin);

  // ---- VALIDAÇÃO OBRIGATÓRIA PÓS-ESCRITA DO GLB
  console.log('· decodificando e validando GLB final');
  const verifyDoc = await io.read(join(OUT, 'nature-kit.glb'));
  const verifyPrims = verifyDoc.getRoot().listMeshes().flatMap((m) => m.listPrimitives());
  const verifyMatNames = new Set(verifyPrims.map((p) => p.getMaterial()?.getName() || 'NONE'));
  console.log(`  primitivas no GLB: ${verifyPrims.length}, materiais sobreviventes:`, [...verifyMatNames]);

  if (verifyMatNames.size <= 1) {
    fail(`Falha crítica: todas as primitivas apontam para apenas um material (${[...verifyMatNames]})! Dedup consolidou indevidamente.`);
  }

  const expectedMaterials = ['palette', 'branch', 'leaves', 'grass', 'flower', 'flowerLeaf'];
  for (const exp of expectedMaterials) {
    if (!verifyMatNames.has(exp)) {
      fail(`Falha crítica: material essencial '${exp}' não sobreviveu no GLB final!`);
    }
  }

  // Verifica compatibilidade de material por tipo
  for (const p of verifyPrims) {
    const matName = p.getMaterial()?.getName();
    const extra = p.getExtras() || {};
    if (extra.type === 'rock' || extra.type === 'cliff' || extra.type === 'pebble' || extra.type === 'mountain') {
      if (matName !== 'palette') fail(`Tipo ${extra.type} usando material indevido: ${matName}`);
    }
  }
  console.log('  validação do GLB concluída com 100% de conformidade!');

  // ---- céu
  let sky = null;
  const hdr = assets.get('UNS_HDRI.hdr');
  const skyPath = join(OUT, 'sky-env.webp');
  if (!skipSky && hdr) {
    console.log('· reduzindo o HDRI (isto leva alguns minutos)');
    const env = hdrToEnvmap(hdr);
    const webp = await sharp(env.rgbe, { raw: { width: env.width, height: env.height, channels: 4 } })
      .webp({ lossless: true, effort: 6 }).toBuffer();
    writeFileSync(skyPath, webp);
    sky = {
      file: 'sky-env.webp', width: env.width, height: env.height, encoding: 'rgbe_webp_lossless',
      formula: 'linear = rgb / 255 * 2^(a*255 - 128)', source_resolution: env.source,
      peak_luminance: env.peak, colors: env.colors, bytes: webp.length, sha256: sha256(webp),
    };
    console.log(`  envmap ${env.width}×${env.height}  ${KB(webp.length)}  zênite ${env.colors.zenith} · horizonte ${env.colors.horizon} · solo ${env.colors.ground}`);
  } else if (existsSync(skyPath)) {
    const webp = readFileSync(skyPath);
    const prev = existsSync(join(OUT, 'kit-manifest.json'))
      ? JSON.parse(readFileSync(join(OUT, 'kit-manifest.json'), 'utf8')).sky : null;
    sky = prev && prev.sha256 === sha256(webp) ? prev : null;
    if (sky) console.log('  envmap do céu mantido (--no-sky)');
  }

  // ---- manifesto
  const manifest = {
    kit_schema: KIT_SCHEMA,
    generated_by: 'demo/tools/build-nature-kit.mjs',
    source: {
      package: relative(ROOT, PKG),
      title: 'Ultimate Nature – Starter',
      author: 'Innerverse Interactive',
      bytes: pkgBytes,
      unit_scale: FBX_SCALE,
      eula: 'Standard Unity Asset Store EULA (https://unity.com/legal/as-terms)',
      eula_faq: 'https://support.unity.com/hc/en-us/articles/34387186019988-Can-I-use-assets-from-the-Asset-Store-with-other-engines',
      note: 'Ativos da Unity Asset Store. Os originais não são alterados nem redistribuídos. Apenas derivados convertidos e otimizados entram no runtime.',
    },
    model: { file: 'nature-kit.glb', bytes: kitBin.length, sha256: sha256(kitBin) },
    materials: Object.fromEntries(Object.entries(MATERIALS).map(([k, m]) => [k, {
      texture: m.texture, color: m.color, alphaTest: m.alphaTest, roughness: m.roughness, foliage: !!m.foliage, doubleSided: !!m.doubleSided,
    }])),
    textures: texIds,
    types,
    sky,
  };
  const manifestBin = Buffer.from(JSON.stringify(manifest, null, 2) + '\n');
  writeFileSync(join(OUT, 'kit-manifest.json'), manifestBin);

  // ---- relatório
  const packages = [
    { label: 'nature-kit.glb', input: inputBytes.fbx, bytes: kitBin.length, sha256: sha256(kitBin) },
    ...texReport,
    ...(sky ? [{ label: 'sky-env.webp', input: hdr ? hdr.length : 0, bytes: sky.bytes, sha256: sky.sha256 }] : []),
    { label: 'kit-manifest.json', input: 0, bytes: manifestBin.length, sha256: sha256(manifestBin) },
  ];
  const total = packages.reduce((s, p) => s + p.bytes, 0);
  const report = {
    generated_at: new Date().toISOString(),
    kit_schema: KIT_SCHEMA,
    source_package_bytes: pkgBytes,
    packages: packages.map((p) => ({ ...p, reduction: p.input ? round(1 - p.bytes / p.input, 4) : null })),
    embedded_total_bytes: total,
    types: Object.fromEntries(Object.entries(types).map(([k, v]) => [k, {
      variants: v.variants,
      triangles: v.items[0].levels.map((l) => l.triangles),
      height_m: v.items[0].height,
    }])),
  };
  writeFileSync(join(OUT, 'kit-report.json'), JSON.stringify(report, null, 2) + '\n');
  rmSync(TMP, { recursive: true, force: true });

  console.log('\n  pacote                 entrada        saída   redução');
  for (const p of report.packages) {
    console.log(`  ${p.label.padEnd(20)} ${KB(p.input).padStart(10)} ${KB(p.bytes).padStart(12)}  ${p.reduction == null ? '—' : (p.reduction * 100).toFixed(1) + '%'}`);
  }
  console.log(`\n  embutido no HTML: ${KB(total)}`);
  console.log(`ok  ${relative(DEMO, OUT)}`);
}

main().catch((e) => { console.error(e); process.exit(1); });
