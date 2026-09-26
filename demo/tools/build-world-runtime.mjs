// Prepara o pacote de runtime do mundo (região comercial + Região Turbulenta) a partir das
// exportações autorais de `blender-world/exports`. As fontes são tratadas como somente leitura.
//
//   node tools/build-world-runtime.mjs            → demo/assets/world-runtime/
//   node tools/build-world-runtime.mjs --high     → também grava a variante de alta densidade
//
// Saídas (todas determinísticas):
//   world-manifest.json     contrato compacto que a demo consome (âncoras, rotas, colisores, instâncias)
//   region-height.bin       Uint16 little-endian, 1025×1025, 1 m por amostra
//   region-mask.bin         nibbles: bits 0-2 bioma, bit 3 água (2 amostras por byte)
//   turbulent-height.bin    Uint16 little-endian, 513×513, 0,5 m por amostra
//   region-props.glb        arquitetura e pontos de interesse (geometria; materiais por nome)
//   region-water.glb        rio principal e canais de irrigação
//   turbulent-props.glb     ruínas, portal, fragmentos suspensos e raízes da Turbulenta
//   nature-prototypes.glb   protótipos de vegetação (artefato separado; não entra no HTML padrão)
//   runtime-report.json     tamanhos de entrada/saída, redução e hash de cada pacote
//
// Nada aqui escreve em `blender-world/`.
import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync, existsSync, statSync, rmSync } from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import sharp from 'sharp';
import { Document, NodeIO } from '@gltf-transform/core';
import { KHRONOS_EXTENSIONS, EXTMeshoptCompression } from '@gltf-transform/extensions';
import { dedup, prune, weld, quantize, reorder, meshopt } from '@gltf-transform/functions';
import { MeshoptEncoder } from 'meshoptimizer';

const TOOLS = dirname(fileURLToPath(import.meta.url));
const DEMO = dirname(TOOLS);
const ROOT = dirname(DEMO);
const SRC = join(ROOT, 'blender-world', 'exports');
const OUT = join(DEMO, 'assets', 'world-runtime');
const RUNTIME_SCHEMA = '1.0.0';
const SUPPORTED_SOURCE_SCHEMA = ['1.0.0'];

const fail = (msg) => { console.error(`build-world-runtime: ${msg}`); process.exit(1); };
const sha256 = (buf) => createHash('sha256').update(buf).digest('hex');
const round = (v, n = 4) => Math.round(v * 10 ** n) / 10 ** n;
const srcPath = (rel) => join(SRC, rel);
const KB = (n) => `${(n / 1024).toFixed(1)} KB`;

// ---------------------------------------------------------------- validação da fonte
function readSourceManifest() {
  const file = srcPath('manifest.json');
  if (!existsSync(file)) fail(`manifesto ausente em ${file}`);
  const raw = readFileSync(file);
  const m = JSON.parse(raw.toString('utf8'));
  if (!SUPPORTED_SOURCE_SCHEMA.includes(m.schema_version)) {
    fail(`schema_version "${m.schema_version}" não suportado (esperado ${SUPPORTED_SOURCE_SCHEMA.join(', ')})`);
  }
  const region = m.scenes?.REGIAO_COMERCIAL, turb = m.scenes?.TURBULENTA_01;
  if (!region) fail('cena REGIAO_COMERCIAL ausente no manifesto');
  if (!turb) fail('cena TURBULENTA_01 ausente no manifesto');
  const expectRegion = [-512, -512, 512, 512], expectTurb = [-128, -128, 128, 128];
  const same = (a, b) => a.length === b.length && a.every((v, i) => Math.abs(v - b[i]) < 1e-6);
  if (!same(region.bounds_m, expectRegion)) fail(`limites da região inesperados: ${region.bounds_m}`);
  if (!same(turb.bounds_m, expectTurb)) fail(`limites da Turbulenta inesperados: ${turb.bounds_m}`);
  if (region.unit_meters !== 1 || turb.unit_meters !== 1) fail('escala das cenas jogáveis deve ser 1 unidade = 1 metro');
  if (!m.height_encoding?.region || !m.height_encoding?.turbulent) fail('height_encoding incompleto');
  for (const k of ['region', 'turbulent']) {
    if (m.height_encoding[k].encoding !== 'linear_uint16') fail(`codificação de altura "${m.height_encoding[k].encoding}" não suportada`);
  }
  return { manifest: m, sha256: sha256(raw), bytes: raw.length };
}

const REQUIRED = [
  'maps/region_height.png', 'maps/region_water.png', 'maps/region_biome.png',
  'maps/turbulent_height.png',
  'region/environment.glb', 'region/nature.glb',
  'region/instances.json', 'region/points_of_interest.json', 'region/routes.json',
  'turbulent/turbulenta_01.glb', 'turbulent/instances.json', 'turbulent/routes.json',
];

function checkSources(manifest) {
  const missing = REQUIRED.filter((rel) => !existsSync(srcPath(rel)));
  if (missing.length) fail(`fontes obrigatórias ausentes:\n  - ${missing.join('\n  - ')}`);
  const declared = new Set(manifest.export_files || []);
  const undeclared = REQUIRED.filter((rel) => !declared.has(rel) && !rel.startsWith('maps/'));
  if (undeclared.length) fail(`fontes usadas mas não declaradas em export_files: ${undeclared.join(', ')}`);
  const chunks = manifest.scenes.REGIAO_COMERCIAL.chunks || [];
  if (chunks.length !== 16) fail(`esperados 16 blocos de terreno, encontrados ${chunks.length}`);
  const chunkMissing = chunks.filter((c) => !existsSync(srcPath(c.file)));
  if (chunkMissing.length) fail(`blocos de terreno ausentes: ${chunkMissing.map((c) => c.id).join(', ')}`);
  const inventory = REQUIRED.map((rel) => {
    const buf = readFileSync(srcPath(rel));
    return { file: rel, bytes: buf.length, sha256: sha256(buf) };
  });
  return inventory;
}

// ---------------------------------------------------------------- mapas
// O PNG de 16 bits precisa ser lido em `grey16`: a conversão padrão do sharp para RGB aplica
// gama e destrói a escala linear de alturas.
async function readGrey16(file) {
  const { data, info } = await sharp(file).toColourspace('grey16').raw({ depth: 'ushort' }).toBuffer({ resolveWithObject: true });
  if (info.channels !== 1) fail(`${file}: esperado 1 canal em grey16, obtido ${info.channels}`);
  return { u16: new Uint16Array(data.buffer, data.byteOffset, data.length / 2), width: info.width, height: info.height };
}
async function readGrey8(file) {
  const { data, info } = await sharp(file).toColourspace('b-w').raw().toBuffer({ resolveWithObject: true });
  return { u8: data, width: info.width, height: info.height, channels: info.channels };
}

// Uint16 little-endian, ordem linha-a-linha: i = x + 512, j = y_plano + 512.
function packHeight(u16) {
  const out = Buffer.alloc(u16.length * 2);
  for (let k = 0; k < u16.length; k++) out.writeUInt16LE(u16[k], k * 2);
  return out;
}

// bits 0-2 bioma (0-5), bit 3 água; duas amostras por byte (baixa primeiro)
function packMask(biome, water, n) {
  const out = Buffer.alloc(Math.ceil(n / 2));
  for (let k = 0; k < n; k++) {
    const nib = (Math.min(7, biome[k]) & 7) | (water[k] ? 8 : 0);
    const at = k >> 1;
    out[at] = (k & 1) ? (out[at] | (nib << 4)) : ((out[at] & 0xf0) | nib);
  }
  return out;
}

// ---------------------------------------------------------------- glTF
const io = new NodeIO().registerExtensions([...KHRONOS_EXTENSIONS, EXTMeshoptCompression])
  .registerDependencies({ 'meshopt.encoder': MeshoptEncoder });

// Os GLBs trazem as cinco cenas do arquivo mestre; só uma delas tem conteúdo (o exportador nem
// sempre usa o nome da região — `nature.glb` guarda os protótipos em BIBLIOTECA_LOCAL).
function sceneOf(doc, name) {
  const scenes = doc.getRoot().listScenes();
  const named = scenes.find((s) => s.getName() === name && s.listChildren().length);
  if (named) return named;
  const populated = scenes.filter((s) => s.listChildren().length);
  if (populated.length === 1) return populated[0];
  return doc.getRoot().getDefaultScene() || scenes.find((s) => s.getName() === name) || null;
}

// Caixa envolvente do nó no espaço de mundo (TRS do próprio nó e de todos os pais).
function nodeWorldBox(node) {
  const mesh = node.getMesh();
  if (!mesh) return null;
  const m = node.getWorldMatrix();
  const box = { min: [Infinity, Infinity, Infinity], max: [-Infinity, -Infinity, -Infinity] };
  const p = [0, 0, 0];
  for (const prim of mesh.listPrimitives()) {
    const pos = prim.getAttribute('POSITION');
    if (!pos) continue;
    for (let k = 0; k < pos.getCount(); k++) {
      pos.getElement(k, p);
      const x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
      const y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
      const z = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
      if (x < box.min[0]) box.min[0] = x; if (x > box.max[0]) box.max[0] = x;
      if (y < box.min[1]) box.min[1] = y; if (y > box.max[1]) box.max[1] = y;
      if (z < box.min[2]) box.min[2] = z; if (z > box.max[2]) box.max[2] = z;
    }
  }
  return box.min[0] === Infinity ? null : box;
}

// Mantém apenas os nós aceitos por `keep`, em uma cena única, e descarta todas as texturas:
// os materiais do novo mundo são resolvidos por nome contra as superfícies PBR que a demo já embute.
async function writeSubset(doc, sceneName, keep, outFile, { label }) {
  const root = doc.getRoot();
  const scene = sceneOf(doc, sceneName);
  if (!scene) fail(`cena ${sceneName} ausente no documento`);
  const kept = [];
  for (const node of [...scene.listChildren()]) {
    if (keep(node.getName(), node)) kept.push(node); else node.dispose();
  }
  for (const other of root.listScenes()) if (other !== scene) other.dispose();
  scene.setName(sceneName);
  root.setDefaultScene(scene);
  for (const mat of root.listMaterials()) {
    mat.setBaseColorTexture(null).setNormalTexture(null).setMetallicRoughnessTexture(null)
      .setOcclusionTexture(null).setEmissiveTexture(null);
  }
  for (const tex of root.listTextures()) tex.dispose();
  await doc.transform(
    dedup(),
    prune({ keepAttributes: false, keepLeaves: false }),
    weld(),
    reorder({ encoder: MeshoptEncoder, target: 'performance' }),
    quantize({ pattern: /^(POSITION|NORMAL|TEXCOORD|COLOR)(_\d+)?$/ }),
    meshopt({ encoder: MeshoptEncoder, level: 'high' }),
  );
  const bin = Buffer.from(await io.writeBinary(doc));
  writeFileSync(outFile, bin);
  return { label, file: relative(DEMO, outFile), bytes: bin.length, sha256: sha256(bin), nodes: kept.length };
}

// ---------------------------------------------------------------- classificação dos props
// Terreno e rotas são reconstruídos do heightfield (1 m contra os 4 m do GLB) e das polilinhas;
// os campos produtivos são quads planos que atravessariam o relevo, e a água sai em pacote próprio.
const SKIP_PROP = /^(REGION_Terrain_|ROUTE_|REGION_MainRiver|REGION_IrrigationCanal|South_ProductiveField)/;
const WATER_NODE = /^(REGION_MainRiver|REGION_IrrigationCanal)/;
const TURB_SKIP = /^TURBULENT_Terrain$/;
// Os campos entram nos dados (mapa e splatting do terreno) mesmo sem geometria própria.
const PLACEMENT_SKIP = /^(REGION_Terrain_|ROUTE_|REGION_MainRiver|REGION_IrrigationCanal)/;

// Agrupa fachada/fundo/telhado de uma mesma construção em um colisor só.
const groupKey = (name) => name.replace(/_(Facade|Rear|Roof)$/, '');
// Elementos atravessáveis ou puramente decorativos não recebem colisor.
const NO_COLLIDER = /ProductiveField|^Bridge_|Portal|_Dock$|ScatterRocks|_Root_|ROUTE_|Fragment/;
const BRIDGE_NODE = /^Bridge_(Main|Minor)$/;

function classify(name) {
  if (/House|Workshop|Warehouse|Stable|Market/.test(name)) return 'building';
  if (/Tower/.test(name)) return 'tower';
  if (/Mine_Rock|Rock_/.test(name)) return 'rock';
  if (/Ruin/.test(name)) return 'ruin';
  if (/Mine_Entrance/.test(name)) return 'mine';
  if (/Cave/.test(name)) return 'cave';
  if (/Observatory/.test(name)) return 'observatory';
  if (/Camp/.test(name)) return 'camp';
  if (/Portal/.test(name)) return 'portal';
  if (/Bridge/.test(name)) return 'bridge';
  if (/Dock/.test(name)) return 'dock';
  if (/DeadTree/.test(name)) return 'dead_tree';
  if (/Fragment/.test(name)) return 'fragment';
  if (/Root/.test(name)) return 'root';
  if (/Field/.test(name)) return 'field';
  return 'prop';
}

// Topo da superfície de madeira: é o tabuleiro das pontes, o piso que o jogador pisa.
function deckLevel(node) {
  const mesh = node.getMesh();
  if (!mesh) return null;
  const m = node.getWorldMatrix();
  const p = [0, 0, 0];
  let top = -Infinity;
  for (const prim of mesh.listPrimitives()) {
    if (!/Wood|Madeira/i.test(prim.getMaterial()?.getName() || '')) continue;
    const pos = prim.getAttribute('POSITION');
    for (let k = 0; k < pos.getCount(); k++) {
      pos.getElement(k, p);
      top = Math.max(top, m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]);
    }
  }
  return top === -Infinity ? null : top;
}

// Folga do tabuleiro sobre o leito do rio, copiada da ponte principal, que a fonte posicionou bem.
const BRIDGE_CLEARANCE = 2.6;

// Colisores e marcos derivados da geometria: o runtime não precisa reabrir o GLB para colidir.
function derivePlacements(doc, sceneName, { skip, offsetPlan = [0, 0], sampleHeight = null }) {
  const scene = sceneOf(doc, sceneName);
  const groups = new Map();
  for (const node of scene.listChildren()) {
    const name = node.getName();
    if (skip.test(name)) continue;
    const box = nodeWorldBox(node);
    if (!box) continue;
    const key = groupKey(name);
    const g = groups.get(key) || { name: key, min: [...box.min], max: [...box.max], parts: [], deck: null };
    for (let i = 0; i < 3; i++) { g.min[i] = Math.min(g.min[i], box.min[i]); g.max[i] = Math.max(g.max[i], box.max[i]); }
    if (BRIDGE_NODE.test(name)) g.deck = deckLevel(node);
    g.parts.push(name);
    groups.set(key, g);
  }
  const placements = [];
  for (const g of groups.values()) {
    // glTF (x, altura, z) → plano do mundo (x, y) com y = -z
    const x = (g.min[0] + g.max[0]) / 2 + offsetPlan[0];
    const y = -(g.min[2] + g.max[2]) / 2 + offsetPlan[1];
    const hw = Math.max(0.2, (g.max[0] - g.min[0]) / 2);
    const hd = Math.max(0.2, (g.max[2] - g.min[2]) / 2);
    const entry = {
      name: g.name, kind: classify(g.name),
      plan: [round(x, 3), round(y, 3)],
      half: [round(hw, 3), round(hd, 3)],
      base: round(g.min[1], 3), top: round(g.max[1], 3),
      collider: !NO_COLLIDER.test(g.name),
      bridge: BRIDGE_NODE.test(g.name),
      parts: g.parts.length,
    };
    if (g.deck != null) {
      entry.deck = round(g.deck, 3);
      // A fonte posicionou a ponte secundária 2,5 m abaixo do leito: o tabuleiro sai enterrado.
      // Em vez de editar a exportação, o pacote registra o quanto o nó precisa subir e o runtime
      // aplica o mesmo deslocamento à geometria, ao colisor e ao piso de `groundHeight`.
      if (sampleHeight) {
        let bed = Infinity;
        for (let dy = -hd; dy <= hd; dy += 1) for (let dx = -hw; dx <= hw; dx += 1) {
          bed = Math.min(bed, sampleHeight(x + dx, y + dy));
        }
        const target = bed + BRIDGE_CLEARANCE;
        const lift = target - g.deck;
        entry.bed = round(bed, 3);
        entry.lift = Math.abs(lift) < 0.2 ? 0 : round(lift, 3);
        entry.deck_final = round(g.deck + entry.lift, 3);
      }
    }
    placements.push(entry);
  }
  placements.sort((a, b) => (a.name < b.name ? -1 : 1));
  return placements;
}

// ---------------------------------------------------------------- dados auxiliares
const readJson = (rel) => JSON.parse(readFileSync(srcPath(rel), 'utf8'));

function compactRoutes(manifest, sceneKey, geometryRel) {
  const declared = manifest.scenes[sceneKey].routes || {};
  const geo = readJson(geometryRel).routes || {};
  const out = {};
  for (const [id, meta] of Object.entries(declared)) {
    const pts = (geo[id]?.points || []).map((p) => [round(p[0], 2), round(p[1], 2), round(p[2], 2)]);   // rotas: 1 cm
    if (!pts.length) fail(`rota ${id} sem geometria em ${geometryRel}`);
    out[id] = {
      anchors: meta.anchors, width_m: meta.width_m, risk: meta.risk,
      max_sustained_slope_deg: meta.max_sustained_slope_deg,
      max_peak_slope_deg: meta.max_peak_slope_deg,
      measured_max_slope_deg: geo[id]?.measured_max_slope_deg ?? null,
      points: pts,
    };
  }
  return out;
}

// As âncoras do manifesto trazem só (x, y); a altura vem dos pontos de interesse, quando existir.
function compactAnchors(manifest, sceneKey, poiRel, sampleHeight) {
  const anchors = manifest.scenes[sceneKey].anchors || {};
  const poi = readJson(poiRel).points_of_interest || [];
  const byId = new Map(poi.map((p) => [p.id, p.location_m]));
  const out = {};
  for (const [id, xy] of Object.entries(anchors)) {
    const loc = byId.get(id);
    const h = loc ? loc[2] : sampleHeight(xy[0], xy[1]);
    out[id] = [round(xy[0], 3), round(xy[1], 3), round(h, 3)];
  }
  // pontos de interesse extras (satélites) viram âncoras derivadas
  for (const p of poi) {
    if (out[p.id]) continue;
    out[p.id] = [round(p.location_m[0], 3), round(p.location_m[1], 3), round(p.location_m[2], 3)];
  }
  return out;
}

function compactInstances(rel) {
  const src = readJson(rel);
  const groups = [];
  for (const g of src.groups || []) {
    const pts = (g.points_m || []).map((p) => [round(p[0], 1), round(p[1], 1), round(p[2], 1)]);
    groups.push({
      id: g.id, count: pts.length, source_object: g.source_object,
      rotation: g.rotation ?? null, scale_range: g.scale_range ?? null, points: pts,
    });
  }
  return groups;
}

// ---------------------------------------------------------------- água
// O rio exportado pelo Blender é uma faixa plana; o leito real sobe e desce ao longo do percurso.
// O perfil abaixo acompanha o leito para a lâmina de água não mergulhar dentro do terreno.
function riverProfile(height, mask, hmin, hmax, samples) {
  const W = samples, H = samples;
  const val = (i, j) => hmin + (height.u16[j * W + i] / 65535) * (hmax - hmin);
  const level = new Float64Array(W).fill(NaN);
  const span = new Int32Array(W * 2).fill(-1);
  for (let i = 0; i < W; i++) {
    let lo = Infinity, jmin = -1, jmax = -1;
    for (let j = 0; j < H; j++) {
      if (!(mask[j * W + i] & 8)) continue;
      const h = val(i, j);
      if (h < lo) lo = h;
      if (jmin < 0) jmin = j;
      jmax = j;
    }
    if (jmin >= 0) { level[i] = lo; span[i * 2] = jmin; span[i * 2 + 1] = jmax; }
  }
  // suavização por média móvel (janela de 33 m) e depois monotonização suave: o rio não sobe.
  const smooth = new Float64Array(W);
  for (let i = 0; i < W; i++) {
    let s = 0, n = 0;
    for (let k = -16; k <= 16; k++) {
      const q = i + k;
      if (q < 0 || q >= W || Number.isNaN(level[q])) continue;
      s += level[q]; n++;
    }
    smooth[i] = n ? s / n : NaN;
  }
  const depth = 0.45;
  const out = [];
  const step = 8;   // amostra a cada 8 m; o runtime interpola
  for (let i = 0; i < W; i += step) out.push(Number.isNaN(smooth[i]) ? null : round(smooth[i] + depth, 3));
  if ((W - 1) % step) out.push(Number.isNaN(smooth[W - 1]) ? null : round(smooth[W - 1] + depth, 3));
  const finite = out.filter((v) => v !== null).sort((a, b) => a - b);
  return {
    step_m: step,
    samples: out,
    median_m: finite.length ? round(finite[finite.length >> 1], 3) : 0,
    min_m: finite.length ? finite[0] : 0,
    max_m: finite.length ? finite[finite.length - 1] : 0,
    cells: span.reduce((n, v, k) => (k % 2 === 0 && v >= 0 ? n + 1 : n), 0),
  };
}

// ---------------------------------------------------------------- validação cruzada
async function validateChunks(manifest, height, hmin, hmax) {
  const W = height.width;
  const val = (i, j) => hmin + (height.u16[j * W + i] / 65535) * (hmax - hmin);
  let worst = 0, worstAt = null, verts = 0;
  for (const chunk of manifest.scenes.REGIAO_COMERCIAL.chunks) {
    const doc = await io.read(srcPath(chunk.file));
    for (const node of doc.getRoot().listNodes()) {
      const mesh = node.getMesh();
      if (!mesh) continue;
      for (const prim of mesh.listPrimitives()) {
        const pos = prim.getAttribute('POSITION');
        const e = [0, 0, 0];
        for (let k = 0; k < pos.getCount(); k++) {
          pos.getElement(k, e);
          const i = Math.round(e[0] + 512), j = Math.round(-e[2] + 512);
          if (i < 0 || j < 0 || i >= W || j >= W) continue;
          const d = Math.abs(val(i, j) - e[1]);
          if (d > worst) { worst = d; worstAt = [e[0], -e[2]]; }
          verts++;
        }
      }
    }
  }
  return { vertices: verts, max_error_m: round(worst, 5), at_plan: worstAt };
}

async function validateTurbulentTerrain(height, hmin, hmax) {
  const W = height.width;                       // 513 amostras para 256 m → 0,5 m
  const val = (i, j) => hmin + (height.u16[j * W + i] / 65535) * (hmax - hmin);
  const doc = await io.read(srcPath('turbulent/turbulenta_01.glb'));
  const node = doc.getRoot().listNodes().find((n) => n.getName() === 'TURBULENT_Terrain');
  if (!node) fail('TURBULENT_Terrain ausente em turbulenta_01.glb');
  let worst = 0, verts = 0;
  const e = [0, 0, 0];
  for (const prim of node.getMesh().listPrimitives()) {
    const pos = prim.getAttribute('POSITION');
    for (let k = 0; k < pos.getCount(); k++) {
      pos.getElement(k, e);
      const i = Math.round((e[0] + 128) * 2), j = Math.round((-e[2] + 128) * 2);
      if (i < 0 || j < 0 || i >= W || j >= W) continue;
      const d = Math.abs(val(i, j) - e[1]);
      if (d > worst) worst = d;
      verts++;
    }
  }
  return { vertices: verts, max_error_m: round(worst, 5) };
}

// ---------------------------------------------------------------- protótipos de vegetação
async function prototypeMetrics(doc, sceneName) {
  const scene = sceneOf(doc, sceneName);
  const out = {};
  for (const node of scene.listChildren()) {
    const box = nodeWorldBox(node);
    if (!box) continue;
    const t = node.getTranslation();
    out[node.getName()] = {
      height_m: round(box.max[1] - box.min[1], 3),
      radius_m: round(Math.max(box.max[0] - t[0], t[0] - box.min[0], box.max[2] - t[2], t[2] - box.min[2]), 3),
      base_m: round(box.min[1], 3),
    };
  }
  return out;
}

// ---------------------------------------------------------------- main
async function main() {
  const wantHigh = process.argv.includes('--high');
  await MeshoptEncoder.ready;
  mkdirSync(OUT, { recursive: true });

  console.log('· validando manifesto de origem');
  const { manifest, sha256: manifestHash, bytes: manifestBytes } = readSourceManifest();
  const sourceInventory = checkSources(manifest);
  const region = manifest.scenes.REGIAO_COMERCIAL;
  const turb = manifest.scenes.TURBULENTA_01;
  const rEnc = manifest.height_encoding.region, tEnc = manifest.height_encoding.turbulent;

  console.log('· lendo mapas');
  const rHeight = await readGrey16(srcPath('maps/region_height.png'));
  const rWater = await readGrey8(srcPath('maps/region_water.png'));
  const rBiome = await readGrey8(srcPath('maps/region_biome.png'));
  const tHeight = await readGrey16(srcPath('maps/turbulent_height.png'));
  if (rHeight.width !== 1025 || rHeight.height !== 1025) fail(`heightmap da região com ${rHeight.width}×${rHeight.height} amostras (esperado 1025×1025)`);
  if (tHeight.width !== 513 || tHeight.height !== 513) fail(`heightmap da Turbulenta com ${tHeight.width}×${tHeight.height} amostras (esperado 513×513)`);
  if (rWater.width !== 1025 || rBiome.width !== 1025) fail('máscaras da região com resolução diferente do heightmap');

  const n = 1025 * 1025;
  const biomeIdx = new Uint8Array(n), waterBit = new Uint8Array(n);
  const biomeSeen = new Set();
  for (let k = 0; k < n; k++) {
    const b = rBiome.u8[k * rBiome.channels];
    biomeIdx[k] = b / 32;                      // o exportador multiplica o índice por 32
    biomeSeen.add(biomeIdx[k]);
    waterBit[k] = rWater.u8[k * rWater.channels] > 127 ? 1 : 0;
  }
  for (const b of biomeSeen) if (!Number.isInteger(b) || b > 7) fail(`índice de bioma inválido: ${b}`);

  const heightBin = packHeight(rHeight.u16);
  const maskBin = packMask(biomeIdx, waterBit, n);
  const turbHeightBin = packHeight(tHeight.u16);
  writeFileSync(join(OUT, 'region-height.bin'), heightBin);
  writeFileSync(join(OUT, 'region-mask.bin'), maskBin);
  writeFileSync(join(OUT, 'turbulent-height.bin'), turbHeightBin);

  console.log('· conferindo heightfield contra os 16 blocos exportados');
  const chunkCheck = await validateChunks(manifest, rHeight, rEnc.min, rEnc.max);
  if (chunkCheck.max_error_m > 0.01) fail(`heightmap divergente dos blocos GLB (erro ${chunkCheck.max_error_m} m)`);
  const turbCheck = await validateTurbulentTerrain(tHeight, tEnc.min, tEnc.max);
  if (turbCheck.max_error_m > 0.05) fail(`heightmap da Turbulenta divergente do GLB (erro ${turbCheck.max_error_m} m)`);

  const W0 = 1025;
  const sampleRegionHeight = (x, y) => {
    const i = Math.max(0, Math.min(W0 - 1, Math.round(x + 512)));
    const j = Math.max(0, Math.min(W0 - 1, Math.round(y + 512)));
    return rEnc.min + (rHeight.u16[j * W0 + i] / 65535) * (rEnc.max - rEnc.min);
  };

  console.log('· extraindo geometria funcional');
  const envBytes = statSync(srcPath('region/environment.glb')).size;
  // `environment.glb` já reúne terreno, rotas, água e arquitetura sem duplicação interna; é a única
  // fonte usada para a região (carregá-lo junto de architecture/nature/routes/waterworks duplicaria tudo).
  const envDoc = await io.read(srcPath('region/environment.glb'));
  const placements = derivePlacements(envDoc, 'REGIAO_COMERCIAL', { skip: PLACEMENT_SKIP, sampleHeight: sampleRegionHeight });
  const propsDoc = await io.read(srcPath('region/environment.glb'));
  const propsOut = await writeSubset(propsDoc, 'REGIAO_COMERCIAL',
    (name) => !SKIP_PROP.test(name), join(OUT, 'region-props.glb'), { label: 'region-props' });
  const waterDoc = await io.read(srcPath('region/environment.glb'));
  const waterOut = await writeSubset(waterDoc, 'REGIAO_COMERCIAL',
    (name) => WATER_NODE.test(name), join(OUT, 'region-water.glb'), { label: 'region-water' });

  const turbBytes = statSync(srcPath('turbulent/turbulenta_01.glb')).size;
  const turbGeoDoc = await io.read(srcPath('turbulent/turbulenta_01.glb'));
  const turbPlacements = derivePlacements(turbGeoDoc, 'TURBULENTA_01', { skip: TURB_SKIP });
  const turbDoc = await io.read(srcPath('turbulent/turbulenta_01.glb'));
  const turbOut = await writeSubset(turbDoc, 'TURBULENTA_01',
    (name) => !TURB_SKIP.test(name), join(OUT, 'turbulent-props.glb'), { label: 'turbulent-props' });

  const natureBytes = statSync(srcPath('region/nature.glb')).size;
  const natMetricsDoc = await io.read(srcPath('region/nature.glb'));
  const protoMetrics = await prototypeMetrics(natMetricsDoc, 'REGIAO_COMERCIAL');
  const natDoc = await io.read(srcPath('region/nature.glb'));
  const natureOut = await writeSubset(natDoc, 'REGIAO_COMERCIAL', () => true,
    join(OUT, 'nature-prototypes.glb'), { label: 'nature-prototypes' });

  console.log('· montando manifesto compacto');
  const W = W0;
  const sampleRegion = sampleRegionHeight;
  const TW = 513;
  const sampleTurb = (x, y) => {
    const i = Math.max(0, Math.min(TW - 1, Math.round((x + 128) * 2)));
    const j = Math.max(0, Math.min(TW - 1, Math.round((y + 128) * 2)));
    return tEnc.min + (tHeight.u16[j * TW + i] / 65535) * (tEnc.max - tEnc.min);
  };

  const maskNibble = new Uint8Array(n);
  for (let k = 0; k < n; k++) maskNibble[k] = (biomeIdx[k] & 7) | (waterBit[k] ? 8 : 0);
  const water = riverProfile(rHeight, maskNibble, rEnc.min, rEnc.max, W);

  const regionInstances = compactInstances('region/instances.json');
  const turbInstances = compactInstances('turbulent/instances.json');

  // Centro do bosque: célula de 128 m com mais folhosas e pinheiros, refinada pela média local.
  // Serve como âncora derivada para o "Bosque das Forjas", que a fonte não nomeia.
  const canopy = regionInstances.filter((g) => /Broadleaf|Pines/i.test(g.id)).flatMap((g) => g.points);
  let forest = null;
  if (canopy.length) {
    const CELL = 128, SPAN = 1024 / CELL;
    const bins = new Map();
    for (const p of canopy) {
      const k = `${Math.floor((p[0] + 512) / CELL)},${Math.floor((p[1] + 512) / CELL)}`;
      bins.set(k, (bins.get(k) || 0) + 1);
    }
    let best = null, bestN = 0;
    for (const [k, count] of bins) if (count > bestN) { bestN = count; best = k.split(',').map(Number); }
    const cx0 = -512 + (best[0] + 0.5) * CELL, cy0 = -512 + (best[1] + 0.5) * CELL;
    const near = canopy.filter((p) => Math.hypot(p[0] - cx0, p[1] - cy0) < CELL);
    const cx = near.reduce((s, p) => s + p[0], 0) / near.length;
    const cy = near.reduce((s, p) => s + p[1], 0) / near.length;
    const rad = Math.sqrt(near.reduce((s, p) => s + (p[0] - cx) ** 2 + (p[1] - cy) ** 2, 0) / near.length) * 1.6;
    forest = { plan: [round(cx, 2), round(cy, 2)], radius_m: round(Math.min(rad, CELL), 2), samples: near.length, grid_m: CELL, span: SPAN };
  }

  const compact = {
    runtime_schema: RUNTIME_SCHEMA,
    world_layout_version: 2,
    generated_by: 'demo/tools/build-world-runtime.mjs',
    source: {
      generator: manifest.generator, blender_version: manifest.blender_version,
      schema_version: manifest.schema_version, seeds: manifest.seeds,
      manifest_sha256: manifestHash, manifest_bytes: manifestBytes,
      axis_note: 'plano do Blender (x, y) → three.js (x, altura, -y)',
      files: sourceInventory,
    },
    axis: { plan_to_three: [1, -1], three_to_plan: [1, -1] },
    region: {
      id: 'REGIAO_COMERCIAL',
      bounds_m: region.bounds_m,
      unit_meters: region.unit_meters,
      height: {
        file: 'region-height.bin', samples: [1025, 1025], step_m: 1,
        encoding: 'linear_uint16_le', origin_m: [-512, -512],
        min: rEnc.min, max: rEnc.max,
        formula: 'h = min + (u16 / 65535) * (max - min); i = x + 512; j = y + 512',
        bytes: heightBin.length, sha256: sha256(heightBin),
      },
      mask: {
        file: 'region-mask.bin', samples: [1025, 1025], step_m: 1,
        encoding: 'nibble_le', layout: 'bits 0-2 = bioma, bit 3 = água; 2 amostras por byte',
        biome_count: Math.max(...biomeSeen) + 1, water_biome_index: 5,
        bytes: maskBin.length, sha256: sha256(maskBin),
      },
      water: {
        level_m: water.median_m, profile: water, surface_source: 'REGION_MainRiver',
        river_crossings_m: region.water_bodies?.main_river?.crossings_m || [],
      },
      anchors: compactAnchors(manifest, 'REGIAO_COMERCIAL', 'region/points_of_interest.json', sampleRegion),
      routes: compactRoutes(manifest, 'REGIAO_COMERCIAL', 'region/routes.json'),
      chunks: region.chunks.map((c) => ({
        id: c.id, row: c.row, col: c.col,
        bounds_m: [-512 + c.col * 256, -512 + c.row * 256, -512 + (c.col + 1) * 256, -512 + (c.row + 1) * 256],
      })),
      placements,
      instances: regionInstances,
      derived: { forest },
      props: { file: 'region-props.glb', bytes: propsOut.bytes, sha256: propsOut.sha256 },
      waterworks: { file: 'region-water.glb', bytes: waterOut.bytes, sha256: waterOut.sha256 },
    },
    turbulent: {
      id: 'TURBULENTA_01',
      bounds_m: turb.bounds_m,
      unit_meters: turb.unit_meters,
      height: {
        file: 'turbulent-height.bin', samples: [513, 513], step_m: 0.5,
        encoding: 'linear_uint16_le', origin_m: [-128, -128],
        min: tEnc.min, max: tEnc.max,
        formula: 'h = min + (u16 / 65535) * (max - min); i = (x + 128) * 2; j = (y + 128) * 2',
        bytes: turbHeightBin.length, sha256: sha256(turbHeightBin),
      },
      anchors: compactAnchors(manifest, 'TURBULENTA_01', 'turbulent/points_of_interest.json', sampleTurb),
      routes: compactRoutes(manifest, 'TURBULENTA_01', 'turbulent/routes.json'),
      placements: turbPlacements,
      instances: turbInstances,
      props: { file: 'turbulent-props.glb', bytes: turbOut.bytes, sha256: turbOut.sha256 },
      // `turbulent/points_of_interest.json` vem vazio na fonte: as âncoras acima vêm do manifesto principal.
      poi_source: 'manifest.scenes.TURBULENTA_01.anchors',
    },
    nature_prototypes: {
      file: 'nature-prototypes.glb', bytes: natureOut.bytes, sha256: natureOut.sha256,
      embedded_in_html: false, metrics: protoMetrics,
      note: 'medidas usadas para calibrar a escala das instâncias; a demo desenha a vegetação com seus próprios protótipos',
    },
    validation: {
      region_chunks: chunkCheck,
      turbulent_terrain: turbCheck,
      biome_values: [...biomeSeen].sort((a, b) => a - b),
      river_columns: water.cells,
    },
  };

  const manifestOut = Buffer.from(JSON.stringify(compact) + '\n');
  writeFileSync(join(OUT, 'world-manifest.json'), manifestOut);

  // Variante opcional de alta densidade: mesmos dados, protótipos e props sem quantização agressiva.
  if (wantHigh) {
    const hiDoc = await io.read(srcPath('region/environment.glb'));
    const hi = await writeSubset(hiDoc, 'REGIAO_COMERCIAL', (name) => !SKIP_PROP.test(name),
      join(OUT, 'region-props.high.glb'), { label: 'region-props.high' });
    console.log(`  variante alta: ${hi.file} ${KB(hi.bytes)}`);
  } else {
    rmSync(join(OUT, 'region-props.high.glb'), { force: true });
  }

  const packages = [
    { label: 'region-height.bin', input: statSync(srcPath('maps/region_height.png')).size, bytes: heightBin.length, sha256: sha256(heightBin) },
    { label: 'region-mask.bin', input: statSync(srcPath('maps/region_biome.png')).size + statSync(srcPath('maps/region_water.png')).size, bytes: maskBin.length, sha256: sha256(maskBin) },
    { label: 'turbulent-height.bin', input: statSync(srcPath('maps/turbulent_height.png')).size, bytes: turbHeightBin.length, sha256: sha256(turbHeightBin) },
    { label: 'region-props.glb', input: envBytes, bytes: propsOut.bytes, sha256: propsOut.sha256 },
    { label: 'region-water.glb', input: envBytes, bytes: waterOut.bytes, sha256: waterOut.sha256 },
    { label: 'turbulent-props.glb', input: turbBytes, bytes: turbOut.bytes, sha256: turbOut.sha256 },
    { label: 'nature-prototypes.glb', input: natureBytes, bytes: natureOut.bytes, sha256: natureOut.sha256 },
    { label: 'world-manifest.json', input: manifestBytes, bytes: manifestOut.length, sha256: sha256(manifestOut) },
  ];
  const embedded = packages.filter((p) => p.label !== 'nature-prototypes.glb');
  const report = {
    generated_at: new Date().toISOString(),
    runtime_schema: RUNTIME_SCHEMA,
    source_root: relative(ROOT, SRC),
    source_total_bytes: sourceInventory.reduce((s, f) => s + f.bytes, 0),
    packages: packages.map((p) => ({ ...p, reduction: round(1 - p.bytes / Math.max(1, p.input), 4) })),
    embedded_total_bytes: embedded.reduce((s, p) => s + p.bytes, 0),
    validation: compact.validation,
    textures: {
      generated: 0,
      strategy: 'as superfícies PBR (WebP) já embutidas em demo/assets/nature cobrem os seis conjuntos CC0 usados pelo mundo; nenhuma textura nova é gerada',
      reused: ['sparse_grass', 'rocky_trail_02', 'rock_3', 'dark_rock', 'bark_brown_02', 'pine_bark'],
    },
  };
  writeFileSync(join(OUT, 'runtime-report.json'), JSON.stringify(report, null, 2) + '\n');

  console.log('\n  pacote                  entrada        saída     redução');
  for (const p of report.packages) {
    console.log(`  ${p.label.padEnd(22)} ${KB(p.input).padStart(10)} ${KB(p.bytes).padStart(12)}  ${(p.reduction * 100).toFixed(1)}%`);
  }
  console.log(`\n  embutido no HTML: ${KB(report.embedded_total_bytes)}`);
  console.log(`  blocos conferidos: ${chunkCheck.vertices} vértices, erro máx ${chunkCheck.max_error_m} m`);
  console.log(`  Turbulenta: ${turbCheck.vertices} vértices, erro máx ${turbCheck.max_error_m} m`);
  console.log(`ok  ${relative(DEMO, OUT)}`);
}

main().catch((err) => { console.error(err); process.exit(1); });
