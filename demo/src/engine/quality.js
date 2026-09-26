// Qualidade gráfica: Alta / Média / Baixa, troca ao vivo e ajuste automático nos primeiros segundos.
//
// Com o mundo de 1.024 × 1.024 m, o nível passou a controlar também o alcance do cenário:
// distância dos blocos de terreno visíveis, distância do nível de detalhe alto, densidade da
// vegetação, sombras da folhagem, alcance da sombra do sol, densidade de grama e a quantidade de
// efeitos suspensos da Região Turbulenta.
import { U } from './materials.js';
import { QREG, updateGrassLOD, updateNatureLOD } from './props.js';
import { updateCharacterShadows } from './characters.js';
import { setSecondaryDensity } from '../world/world-instances.js';
import { setTurbulentFx } from '../world/scene-world.js';
import { setLODBands, updateLODFields, lodMeshes } from './lod-field.js';
import { setGroundCoverQuality, updateGroundCover, coverMeshes } from '../world/ground-cover.js';

// Floresta: as árvores nunca rareiam por qualidade; o nível só muda a distância de cada detalhe
// (`treeLod`: L0, L1, L2 e impostor; 0 desliga o nível) e o alcance das sombras da vegetação.
// Os itens pequenos do sub-bosque (`secondary`) e a cobertura do chão rareiam um pouco.
export const LEVELS = {
  alta: {
    label: 'Alta', dpr: 1.5, post: true, gtao: true, bloom: true, smaa: true, shadow: 4096,
    grass: 1, grassFar: 80,
    vegetation: 1, secondary: 1, vegShadow: true,
    tileNear: 300, blockFar: 1600, shadowSpan: 70, shadowFar: 340, turbFx: 1,
    natureFar: 360, canopyFar: 700, groundFar: 140,
    treeLod: [28, 65, 150, 700], shrubLod: [22, 50, 360], vegShadowFar: 55, coverRadius: 84, coverDensity: 1,
  },
  media: {
    label: 'Média', dpr: 1.25, post: true, gtao: false, bloom: true, smaa: true, shadow: 2048,
    grass: 0.45, grassFar: 64,
    vegetation: 0.72, secondary: 0.85, vegShadow: true,
    tileNear: 200, blockFar: 1100, shadowSpan: 58, shadowFar: 280, turbFx: 0.7,
    natureFar: 260, canopyFar: 500, groundFar: 100,
    treeLod: [0, 45, 120, 520], shrubLod: [0, 40, 260], vegShadowFar: 45, coverRadius: 68, coverDensity: 0.6,
  },
  baixa: {
    label: 'Baixa', dpr: 1, post: false, gtao: false, bloom: false, smaa: false, shadow: 1024,
    grass: 0.20, grassFar: 45,
    vegetation: 0.45, secondary: 0.65, vegShadow: false,
    tileNear: 130, blockFar: 700, shadowSpan: 46, shadowFar: 220, turbFx: 0.4,
    natureFar: 180, canopyFar: 350, groundFar: 70,
    treeLod: [0, 0, 70, 380], shrubLod: [0, 0, 180], vegShadowFar: 0, coverRadius: 48, coverDensity: 0.35,
  },
};
const ORDER = ['alta', 'media', 'baixa'];
const KEY = 'projeto-game-qualidade';
export const QUALITY = { level: 'alta', chosen: false };

export function initialQuality() {
  try {
    const v = localStorage.getItem(KEY);
    if (v && LEVELS[v]) { QUALITY.chosen = true; return v; }
  } catch { /* sem armazenamento: começa em Alta com ajuste automático */ }
  return 'alta';
}

export function applyQuality(ctx, level, save = false) {
  const q = LEVELS[level];
  QUALITY.level = level;
  ctx.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, q.dpr));
  ctx.renderer.setSize(window.innerWidth, window.innerHeight);
  const sh = ctx.sun.shadow;
  if (sh.mapSize.x !== q.shadow) {
    sh.mapSize.set(q.shadow, q.shadow);
    if (sh.map) { sh.map.dispose(); sh.map = null; }
  }
  // alcance da sombra do sol: o volume acompanha o jogador, então basta ajustar a extensão
  const sc = sh.camera;
  if (sc.right !== q.shadowSpan || sc.far !== q.shadowFar) {
    sc.left = -q.shadowSpan; sc.right = q.shadowSpan;
    sc.top = q.shadowSpan; sc.bottom = -q.shadowSpan;
    sc.far = q.shadowFar;
    sc.updateProjectionMatrix();
  }
  ctx.setupPost(q);
  ctx.dirty = true;
  QREG.grassOn = q.grass > 0;
  QREG.natureShadow = q.vegShadow;
  QREG.natureFar = q.natureFar;
  QREG.canopyFar = q.canopyFar || q.natureFar;
  QREG.groundFar = q.groundFar || 120;
  for (const r of QREG.grass) r.mesh.count = Math.floor(r.full * q.grass);
  U.uGrassFar.value = q.grassFar;
  setSecondaryDensity(q.secondary);
  setLODBands({ tree: q.treeLod, shrub: q.shrubLod, shadowFar: q.vegShadow ? q.vegShadowFar : 0 });
  setGroundCoverQuality({ radius: q.coverRadius, density: q.coverDensity });
  setTurbulentFx(q.turbFx);
  if (save) {
    QUALITY.chosen = true;
    try { localStorage.setItem(KEY, level); } catch { /* ignora */ }
  }
}

// Detalhe por distância, a cada quadro: grama só perto da câmera e sombra só dos personagens próximos
export function updateDetail(cam, focus) {
  updateGrassLOD(cam, U.uGrassFar.value);
  updateNatureLOD(cam, QUALITY.level);
  updateVegetation(cam);
  updateCharacterShadows(focus.x, focus.z);
}

// Floresta (nível de detalhe por instância) e cobertura do chão em volta da câmera.
export function updateVegetation(cam) {
  updateLODFields(cam);
  updateGroundCover(cam);
}

// Ajuste automático: ignora o aquecimento, mede ~4 s; se a mediana passar de ~30 ms, desce um nível (até duas vezes)
let samples = [], warm = 0, steps = 0;
export function sampleFrame(ctx, ms, onChange) {
  if (QUALITY.chosen || steps >= 2) return;
  // aba oculta ou travada isolada (troca de aba, compilação de shader) não conta como lentidão
  if (document.visibilityState !== 'visible' || ms > 250) { warm = Math.min(warm, 60); return; }
  if (warm < 90) { warm++; return; }
  samples.push(ms);
  if (samples.length < 120) return;
  const med = samples.sort((a, b) => a - b)[samples.length >> 1];
  samples = []; warm = 30;
  const i = ORDER.indexOf(QUALITY.level);
  if (med > 30 && i < ORDER.length - 1) {
    steps++;
    applyQuality(ctx, ORDER[i + 1]);
    onChange(ORDER[i + 1]);
  } else steps = 2;
}

// Compilação antecipada: gera as primeiras células da cobertura em volta de `cam` e deixa todos os
// níveis da vegetação visíveis enquanto `fn` compila os sombreadores (a compilação ignora o que
// está oculto). Depois, tudo volta como estava.
export async function withVegetationVisible(cam, fn) {
  updateLODFields(cam, true);
  updateGroundCover(cam, 4);
  const touched = [...lodMeshes(), ...coverMeshes()].map((m) => [m, m.visible, m.count]);
  for (const [m] of touched) { m.visible = true; if (!m.count) m.count = 1; }
  try { return await fn(); } finally {
    for (const [m, v, n] of touched) { m.visible = v; m.count = n; }
  }
}
