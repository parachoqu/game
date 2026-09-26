// Tradução dos materiais autorais do Blender para as superfícies PBR que a demo já embute.
//
// O mundo do Blender usa oito conjuntos CC0 da Poly Haven (rock_3, dark_rock, rocky_trail_02,
// bark_brown_02, pine_bark, sparse_grass, forrest_ground_01, river_small_rocks) — exatamente os
// mesmos que `assets/nature` guarda em WebP e que o HTML já carrega. Em vez de reempacotar as
// texturas 2K do GLB (cerca de 500 MB somando os conjuntos funcionais), o pacote de runtime
// descarta as imagens e este módulo reencaixa cada material pelo nome, preservando a direção
// visual e a resposta PBR sem custo algum de bytes.
import * as THREE from 'three';
import { surfaceMat, waterMat, patchMaterial, U } from '../engine/materials.js';

// nome do material no GLB → [superfície da demo, cor de base]
const SURFACE_MAP = {
  MAT_Stone_PBR_CC0: ['rochaNatural', '#b2aa9c'],
  MAT_Rock_PBR_CC0: ['rochaEscura', '#8f8a82'],
  MAT_Bark_PBR_CC0: ['cascaNatural', '#7d6650'],
  MAT_PineBark_PBR_CC0: ['cascaPinheiro', '#6e5a45'],
  MAT_Road_PBR_CC0: ['trilha', '#b9a184'],
  MAT_Grass_PBR_CC0: ['gramaCampo', '#8aa055'],
  MAT_PlasterOchre: ['reboco', '#b8906a'],
  MAT_Terracotta: ['telhas', '#9c4f35'],
  MAT_DarkWood: ['madeira', '#4e3726'],
  MAT_CobaltCloth: ['tecido', '#2f64a3'],
  MAT_CampCloth: ['tecido', '#8c4536'],
  MAT_AgedMetal: ['metal', '#5f5c55'],
  MAT_CaveDark: ['alvenaria', '#171a1f'],
  MAT_ProductiveFieldGreen: ['gramaCampo', '#5c7a34'],
  MAT_ProductiveFieldOchre: ['reboco', '#8a6a2c'],
  MAT_FoliageEmerald: ['tecido', '#2c6e5b'],
  MAT_PineFoliageTeal: ['tecido', '#1f5a5c'],
  MAT_TurbulentGround: ['rochaEscura', '#6a5f78'],
};

const cache = new Map();

function emissive(color, intensity, opts = {}) {
  const m = new THREE.MeshStandardMaterial({
    color, emissive: color, emissiveIntensity: intensity,
    roughness: 0.35, metalness: 0, ...opts,
  });
  return m;
}

// Material do portal e das veias da anomalia: pulsa devagar, sem depender de textura.
function anomalyMaterial(color, opacity) {
  const m = new THREE.MeshStandardMaterial({
    color, emissive: color, emissiveIntensity: 1.35, roughness: 0.2, metalness: 0,
    transparent: opacity < 1, opacity, depthWrite: opacity >= 1, side: THREE.DoubleSide,
  });
  m.customProgramCacheKey = () => 'world-anomaly';
  m.onBeforeCompile = (sh) => {
    Object.assign(sh.uniforms, { uTime: U.uTime });
    sh.fragmentShader = 'uniform float uTime;\n' + sh.fragmentShader.replace(
      '#include <emissivemap_fragment>',
      '#include <emissivemap_fragment>\n  totalEmissiveRadiance *= 0.72 + 0.38 * sin(uTime * 1.7);');
  };
  return m;
}

export function worldMaterial(name) {
  if (cache.has(name)) return cache.get(name);
  let m = null;
  if (name === 'MAT_WaterTeal') m = waterMat();
  else if (name === 'MAT_AnomalyCyan') m = anomalyMaterial('#3fd8dc', 1);
  else if (name === 'MAT_AnomalyCyanSurface') m = anomalyMaterial('#2aa8bd', 0.6);
  else if (name === 'MAT_Ember') m = emissive('#c7451a', 1.8);
  else {
    const entry = SURFACE_MAP[name];
    if (entry) m = surfaceMat(entry[0], entry[1]);
  }
  cache.set(name, m);
  return m;
}

// Resolvedor usado pelo carregador de cenário: `null` mantém o material original do GLB.
export const resolveWorldMaterial = (name) => worldMaterial(name);

// Materiais que devem receber o recorte pontilhado quando ficam entre a câmera e o personagem.
export function patchOcclusion(material) {
  if (!material || material.userData.occPatched) return material;
  patchMaterial(material, ['occ']);
  material.userData.occPatched = true;
  return material;
}
