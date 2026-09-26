// Prepara a grama de campo da demo a partir do "Grass Medium 01" do Poly Haven (CC0).
//
//   node tools/build-meadow-grass.mjs   → demo/assets/meadow-grass/
//
// A fonte é `modelos 3d animados/texturas/terreno/sparse_grass/grass_medium_01_2k.fbx`. Apesar da
// extensão, o download do Poly Haven é um ZIP com o .fbx e a pasta `textures/`; um .fbx comum com
// `textures/` ao lado também serve. Nada é escrito na pasta da fonte.
//
// Saídas:
//   meadow-grass.glb       touceiras em três níveis, meshopt:
//                            L0  lâminas, simplificadas ao longo do comprimento (perto)
//                            L1  só as lâminas maiores, bem simplificadas (média distância)
//                            L2  três cartões cruzados com as fotos de touceira do próprio atlas
//   meadow.webp            atlas das lâminas em cinza de detalhe + recorte no alfa. O tom vem do
//                          material e da cor por instância, como na grama do kit (textura branca ×
//                          cor), para as duas gramas falarem a mesma língua na cena
//   meadow-manifest.json   variantes, medidas, triângulos por nível
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { inflateRawSync } from 'node:zlib';
import sharp from 'sharp';
import { Document, NodeIO } from '@gltf-transform/core';
import { KHRONOS_EXTENSIONS, EXTMeshoptCompression } from '@gltf-transform/extensions';
import { weld, reorder, quantize, meshopt } from '@gltf-transform/functions';
import { MeshoptEncoder, MeshoptDecoder, MeshoptSimplifier } from 'meshoptimizer';

const TOOLS = dirname(fileURLToPath(import.meta.url));
const DEMO = dirname(TOOLS);
const ROOT = dirname(DEMO);
const SRC = join(ROOT, 'modelos 3d animados', 'texturas', 'terreno', 'sparse_grass', 'grass_medium_01_2k.fbx');
const OUT = join(DEMO, 'assets', 'meadow-grass');
const TMP = join(tmpdir(), 'projeto-game-meadow');
const FBX2GLTF = join(TOOLS, 'node_modules/fbx2gltf/bin',
  process.platform === 'win32' ? 'Windows_NT' : process.platform === 'darwin' ? 'Darwin' : 'Linux',
  process.platform === 'win32' ? 'FBX2glTF.exe' : 'FBX2glTF');

const fail = (m) => { console.error(`build-meadow-grass: ${m}`); process.exit(1); };
const sha256 = (b) => createHash('sha256').update(b).digest('hex');
const round = (v, n = 3) => Math.round(v * 10 ** n) / 10 ** n;
const KB = (n) => `${(n / 1024).toFixed(1)} KB`;

// Escala do jogo: o asset vem em tamanho real (touceira grande com 31 cm de largura e 14 cm de
// altura). A grama do kit tem 50 cm de altura; ×2,4 põe as touceiras entre 25 e 75 cm, na
// mesma família de tamanho.
const SCALE = 2.4;
const TEX_SIZE = 1024;

// Tipos: `meadow` é o tapete (touceiras cheias); `meadowTall` são as touceiras altas com
// pendão, de destaque. As `tiny` do pacote somem na escala do jogo e ficam de fora.
//   l0  fração dos triângulos que o L0 mantém
//   l1  fração da área de lâminas que o L1 mantém (as maiores) e teto de triângulos
const TYPES = {
  meadow: {
    sources: ['large_a', 'large_b', 'large_c', 'mid_a', 'mid_b', 'mid_c', 'small_a', 'small_b'],
    l0: 0.3, l1: { area: 0.5, tris: 110 },
    // cartões: foto larga e densa, foto média e foto baixa espalhada
    cards: ['A', 'B', 'D'],
  },
  meadowTall: {
    sources: ['tall_a', 'tall_b', 'tall_c'],
    l0: 0.55, l1: { area: 0.6, tris: 60 },
    cards: ['E', 'C', 'E'],
  },
};

// Fotos de touceira que o próprio atlas traz para os níveis distantes (pixels no atlas de 2048).
const CARD_PX = {
  A: [380, 1500, 1039, 1812], B: [1211, 1530, 1699, 1789], C: [67, 1785, 411, 2047],
  D: [490, 1800, 1016, 2047], E: [1181, 1740, 1616, 2047],
};

// ---------------------------------------------------------------- ZIP mínimo
// Lê o diretório central (os tamanhos locais podem vir zerados quando há descritor de dados).
function readZip(buf) {
  let e = buf.length - 22;
  while (e >= 0 && buf.readUInt32LE(e) !== 0x06054b50) e--;
  if (e < 0) return null;
  const files = new Map();
  const n = buf.readUInt16LE(e + 10);
  let p = buf.readUInt32LE(e + 16);
  for (let i = 0; i < n; i++) {
    if (buf.readUInt32LE(p) !== 0x02014b50) fail('ZIP corrompido (diretório central)');
    const method = buf.readUInt16LE(p + 10), size = buf.readUInt32LE(p + 20);
    const nl = buf.readUInt16LE(p + 28), xl = buf.readUInt16LE(p + 30), cl = buf.readUInt16LE(p + 32);
    const off = buf.readUInt32LE(p + 42);
    const name = buf.toString('utf8', p + 46, p + 46 + nl);
    const start = off + 30 + buf.readUInt16LE(off + 26) + buf.readUInt16LE(off + 28);
    const data = buf.subarray(start, start + size);
    if (method !== 0 && method !== 8) fail(`${name}: compressão ${method} não suportada`);
    files.set(name, method === 8 ? inflateRawSync(data) : Buffer.from(data));
    p += 46 + nl + xl + cl;
  }
  return files;
}

// Devolve { fbx, diff, alpha } a partir do ZIP do Poly Haven ou de um .fbx com `textures/` ao lado.
function readSource() {
  if (!existsSync(SRC)) fail(`fonte não encontrada: ${SRC}`);
  const buf = readFileSync(SRC);
  const pick = (files, re) => { for (const [k, v] of files) if (re.test(k)) return v; return null; };
  let files;
  if (buf.readUInt32LE(0) === 0x04034b50) files = readZip(buf);
  else {
    files = new Map([['model.fbx', buf]]);
    const dir = join(dirname(SRC), 'textures');
    for (const f of ['grass_medium_01_diff_2k.jpg', 'grass_medium_01_alpha_2k.png']) {
      if (existsSync(join(dir, f))) files.set(`textures/${f}`, readFileSync(join(dir, f)));
    }
  }
  const out = { fbx: pick(files, /\.fbx$/i), diff: pick(files, /_diff_[^/]*\.(jpe?g|png)$/i), alpha: pick(files, /_alpha_[^/]*\.png$/i) };
  for (const [k, v] of Object.entries(out)) if (!v) fail(`peça ausente na fonte: ${k}`);
  return out;
}

// ---------------------------------------------------------------- textura
const smooth = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };

async function buildTexture(diffBuf, alphaBuf) {
  const d = await sharp(diffBuf).removeAlpha().raw().toBuffer({ resolveWithObject: true });
  const a = await sharp(alphaBuf).extractChannel(0).raw({ depth: 'uchar' }).toBuffer({ resolveWithObject: true });
  const { width: W, height: H } = d.info;
  if (a.info.width !== W || a.info.height !== H) fail('alfa e difusa com tamanhos diferentes');
  const N = W * H;
  // média de cada canal onde há lâmina
  const mean = [0, 0, 0];
  let n = 0;
  for (let i = 0; i < N; i++) {
    if (a.data[i] < 128) continue;
    for (let c = 0; c < 3; c++) mean[c] += d.data[i * 3 + c];
    n++;
  }
  for (let c = 0; c < 3; c++) mean[c] /= n;
  // Cinza de detalhe: cada canal dividido pela própria média tira o verde-oliva da foto e deixa
  // só a variação (base mais escura, pontas mais claras, pendões palha); o expoente achata o
  // contraste fotográfico para perto do chapado da grama do kit.
  const GAIN = 0.8, CONTRAST = 0.6;
  const rgba = Buffer.alloc(N * 4);
  for (let i = 0; i < N; i++) {
    for (let c = 0; c < 3; c++) {
      const v = GAIN * Math.pow(d.data[i * 3 + c] / mean[c], CONTRAST);
      rgba[i * 4 + c] = Math.round(Math.min(1, v) * 255);
    }
    // borda do recorte mais firme: o degradê largo da foto viraria halo nos mipmaps
    rgba[i * 4 + 3] = Math.round(smooth(0.3, 0.7, a.data[i] / 255) * 255);
  }
  const webp = await sharp(rgba, { raw: { width: W, height: H, channels: 4 } })
    .resize(TEX_SIZE, TEX_SIZE, { kernel: 'lanczos3' })
    .webp({ quality: 88, alphaQuality: 100, effort: 6 })
    .toBuffer();
  return { webp, mean: mean.map((v) => round(v / 255)), atlas: W };
}

// ---------------------------------------------------------------- geometria
// Lê a malha LOD0 de uma touceira em metros, Y para cima, centrada na origem e na escala do jogo.
function readClump(doc, name) {
  const node = doc.getRoot().listNodes().find((n) => n.getName() === `grass_medium_01_${name}_LOD0`);
  if (!node || !node.getMesh()) fail(`touceira ${name} não encontrada no FBX convertido`);
  const m = [...node.getWorldMatrix()];
  m[12] = m[13] = m[14] = 0;               // o pacote enfileira as touceiras no eixo X
  const pos = [], uv = [], idx = [];
  for (const prim of node.getMesh().listPrimitives()) {
    const P = prim.getAttribute('POSITION'), T = prim.getAttribute('TEXCOORD_0');
    const I = prim.getIndices();
    const base = pos.length / 3, p = [0, 0, 0], t = [0, 0];
    for (let k = 0; k < P.getCount(); k++) {
      P.getElement(k, p); T.getElement(k, t);
      pos.push(
        (m[0] * p[0] + m[4] * p[1] + m[8] * p[2]) * SCALE,
        (m[1] * p[0] + m[5] * p[1] + m[9] * p[2]) * SCALE,
        (m[2] * p[0] + m[6] * p[1] + m[10] * p[2]) * SCALE,
      );
      uv.push(t[0], t[1]);
    }
    const ia = I ? I.getArray() : Array.from({ length: P.getCount() }, (_, k) => k);
    for (const k of ia) idx.push(base + k);
  }
  return { pos: new Float32Array(pos), uv: new Float32Array(uv), idx: new Uint32Array(idx) };
}

function bounds(pos) {
  const min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < pos.length; i += 3) for (let c = 0; c < 3; c++) {
    if (pos[i + c] < min[c]) min[c] = pos[i + c];
    if (pos[i + c] > max[c]) max[c] = pos[i + c];
  }
  return { min, max };
}

// Lâminas = componentes conexos da malha; a área de cada uma decide quem fica no L1.
function blades(idx, nverts) {
  const par = new Int32Array(nverts).map((_, i) => i);
  const find = (a) => { while (par[a] !== a) { par[a] = par[par[a]]; a = par[a]; } return a; };
  for (let i = 0; i < idx.length; i += 3) {
    const a = find(idx[i]), b = find(idx[i + 1]), c = find(idx[i + 2]);
    par[b] = a; par[c] = a;
  }
  const byRoot = new Map();
  for (let i = 0; i < idx.length; i += 3) {
    const r = find(idx[i]);
    if (!byRoot.has(r)) byRoot.set(r, []);
    byRoot.get(r).push(i);
  }
  return [...byRoot.values()];
}
const triArea = (pos, idx, t) => {
  const a = idx[t] * 3, b = idx[t + 1] * 3, c = idx[t + 2] * 3;
  const ux = pos[b] - pos[a], uy = pos[b + 1] - pos[a + 1], uz = pos[b + 2] - pos[a + 2];
  const vx = pos[c] - pos[a], vy = pos[c + 1] - pos[a + 1], vz = pos[c + 2] - pos[a + 2];
  return 0.5 * Math.hypot(uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx);
};

// Simplifica ao longo das lâminas; a UV entra no custo para a textura não escorregar.
function simplify(g, idx, target, error) {
  if (idx.length / 3 <= target) return idx;
  const [out] = MeshoptSimplifier.simplifyWithAttributes(idx, g.pos, 3, g.uv, 2, [0.5, 0.5], null,
    Math.max(3, Math.floor(target) * 3), error, []);
  return out;
}

function level0(g, cfg) {
  return simplify(g, g.idx, g.idx.length / 3 * cfg.l0, 0.02);
}

function level1(g, cfg) {
  const list = blades(g.idx, g.pos.length / 3)
    .map((tris) => ({ tris, area: tris.reduce((s, t) => s + triArea(g.pos, g.idx, t), 0) }))
    .sort((a, b) => b.area - a.area);
  const total = list.reduce((s, b) => s + b.area, 0);
  const keep = [];
  let acc = 0;
  for (const b of list) {
    if (acc >= total * cfg.l1.area) break;
    acc += b.area;
    for (const t of b.tris) keep.push(g.idx[t], g.idx[t + 1], g.idx[t + 2]);
  }
  return simplify(g, new Uint32Array(keep), cfg.l1.tris, 0.08);
}

// Três cartões verticais a 0°, 60° e 120°, cada um com uma foto de touceira do atlas. A largura
// é a das touceiras do tipo; a altura segue a proporção da foto (ou a altura, nas touceiras altas).
function cardCluster(cards, width, height, atlas) {
  const pos = [], uv = [], idx = [];
  cards.forEach((id, q) => {
    const [x0, y0, x1, y1] = CARD_PX[id];
    const aspect = (y1 - y0) / (x1 - x0);
    let w = width, h = width * aspect;
    if (height && h < height * 0.9) { h = height; w = Math.min(width * 1.4, h / aspect); }
    const ang = (q / cards.length) * Math.PI;
    const cx = Math.cos(ang) * w / 2, cz = Math.sin(ang) * w / 2;
    const u0 = x0 / atlas, u1 = x1 / atlas, v0 = y0 / atlas, v1 = y1 / atlas;
    const b = pos.length / 3;
    pos.push(-cx, -0.03, -cz, cx, -0.03, cz, cx, h, cz, -cx, h, -cz);
    uv.push(u0, v1, u1, v1, u1, v0, u0, v0);
    idx.push(b, b + 1, b + 2, b, b + 2, b + 3);
  });
  return { pos: new Float32Array(pos), uv: new Float32Array(uv), idx: new Uint32Array(idx) };
}

// Monta uma primitiva só com as posições e UVs usadas pelos índices. Normais para cima, como a
// grama do kit: a touceira recebe a luz do chão em volta, sem lâmina escura contra o sol.
function addMesh(doc, scene, buffer, material, name, g, idx) {
  const used = new Map();
  const pos = [], uv = [], nor = [], out = new Uint32Array(idx.length);
  for (let i = 0; i < idx.length; i++) {
    let k = used.get(idx[i]);
    if (k === undefined) {
      k = used.size; used.set(idx[i], k);
      const v = idx[i];
      pos.push(g.pos[v * 3], g.pos[v * 3 + 1], g.pos[v * 3 + 2]);
      uv.push(g.uv[v * 2], g.uv[v * 2 + 1]);
      nor.push(0, 1, 0);
    }
    out[i] = k;
  }
  const acc = (arr, type) => doc.createAccessor().setType(type).setArray(arr).setBuffer(buffer);
  const prim = doc.createPrimitive()
    .setAttribute('POSITION', acc(new Float32Array(pos), 'VEC3'))
    .setAttribute('NORMAL', acc(new Float32Array(nor), 'VEC3'))
    .setAttribute('TEXCOORD_0', acc(new Float32Array(uv), 'VEC2'))
    .setIndices(acc(out, 'SCALAR'))
    .setMaterial(material);
  const mesh = doc.createMesh(name).addPrimitive(prim);
  scene.addChild(doc.createNode(name).setMesh(mesh));
  return idx.length / 3;
}

// ---------------------------------------------------------------- main
async function main() {
  await MeshoptEncoder.ready; await MeshoptDecoder.ready; await MeshoptSimplifier.ready;
  if (!existsSync(FBX2GLTF)) fail(`FBX2glTF não encontrado em ${FBX2GLTF} (rode: cd tools && npm install)`);
  mkdirSync(OUT, { recursive: true });
  rmSync(TMP, { recursive: true, force: true });
  mkdirSync(TMP, { recursive: true });

  console.log('· lendo a fonte do Poly Haven');
  const src = readSource();

  console.log('· atlas das lâminas');
  const tex = await buildTexture(src.diff, src.alpha);
  writeFileSync(join(OUT, 'meadow.webp'), tex.webp);
  console.log(`  meadow.webp ${TEX_SIZE}²  ${KB(tex.webp.length)}  (média da foto ${tex.mean.join(' ')})`);

  console.log('· convertendo o FBX');
  const fbxPath = join(TMP, 'grass_medium_01.fbx');
  writeFileSync(fbxPath, src.fbx);
  execFileSync(FBX2GLTF, ['--binary', '--input', fbxPath, '--output', join(TMP, 'grass')], { stdio: 'ignore' });
  const io = new NodeIO()
    .registerExtensions([...KHRONOS_EXTENSIONS, EXTMeshoptCompression])
    .registerDependencies({ 'meshopt.decoder': MeshoptDecoder, 'meshopt.encoder': MeshoptEncoder });
  const srcDoc = await io.read(join(TMP, 'grass.glb'));

  const doc = new Document();
  const buffer = doc.createBuffer();
  const scene = doc.createScene('MEADOW');
  doc.getRoot().setDefaultScene(scene);
  const material = doc.createMaterial('meadow').setAlphaMode('MASK').setAlphaCutoff(0.5)
    .setDoubleSided(true).setRoughnessFactor(1).setMetallicFactor(0);

  const types = {};
  for (const [type, cfg] of Object.entries(TYPES)) {
    const items = [];
    let wSum = 0, hSum = 0;
    cfg.sources.forEach((name, v) => {
      const g = readClump(srcDoc, name);
      const b = bounds(g.pos);
      const width = Math.max(b.max[0] - b.min[0], b.max[2] - b.min[2]), height = b.max[1];
      wSum += width; hSum += height;
      const t0 = addMesh(doc, scene, buffer, material, `${type}_${v}_L0`, g, level0(g, cfg));
      const t1 = addMesh(doc, scene, buffer, material, `${type}_${v}_L1`, g, level1(g, cfg));
      items.push({ source: name, width: round(width), height: round(height), triangles: [g.idx.length / 3, t0, t1] });
    });
    const card = cardCluster(cfg.cards, wSum / cfg.sources.length, type === 'meadowTall' ? hSum / cfg.sources.length : 0, tex.atlas);
    const t2 = addMesh(doc, scene, buffer, material, `${type}_card_L2`, card, card.idx);
    types[type] = { variants: items.length, items, card: { images: cfg.cards, triangles: t2 } };
    console.log(`  ${type.padEnd(10)} ${items.length} variantes · L2 ${t2}t`);
    for (const it of items) console.log(`    ${it.source.padEnd(8)} ${it.width.toFixed(2)} × ${it.height.toFixed(2)} m  ${it.triangles.join(' → ')}t`);
  }

  await doc.transform(
    weld(),
    reorder({ encoder: MeshoptEncoder, target: 'performance' }),
    quantize({ pattern: /^(POSITION|NORMAL|TEXCOORD)(_\d+)?$/ }),
    meshopt({ encoder: MeshoptEncoder, level: 'high' }),
  );
  const glb = Buffer.from(await io.writeBinary(doc));
  writeFileSync(join(OUT, 'meadow-grass.glb'), glb);
  console.log(`· meadow-grass.glb ${KB(glb.length)}`);

  const manifest = {
    schema: '1.0.0',
    generated_by: 'tools/build-meadow-grass.mjs',
    source: { asset: 'Poly Haven — Grass Medium 01 (CC0)', file: 'modelos 3d animados/texturas/terreno/sparse_grass/grass_medium_01_2k.fbx', scale: SCALE },
    texture: { file: 'meadow.webp', size: TEX_SIZE, bytes: tex.webp.length, sha256: sha256(tex.webp) },
    model: { file: 'meadow-grass.glb', bytes: glb.length, sha256: sha256(glb) },
    types,
  };
  writeFileSync(join(OUT, 'meadow-manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
  rmSync(TMP, { recursive: true, force: true });
  console.log('· pronto');
}

main().catch((e) => fail(e.stack || e.message));
