// Manifesto compacto do mundo, preparado por `tools/build-world-runtime.mjs`.
// Este módulo é a única porta de entrada para os dados do mundo: nenhum outro arquivo deve
// repetir limites, âncoras, rotas ou larguras.
import MANIFEST from '../../assets/world-runtime/world-manifest.json';

export const WORLD = MANIFEST;
export const WORLD_LAYOUT_VERSION = MANIFEST.world_layout_version;
export const RUNTIME_SCHEMA = MANIFEST.runtime_schema;

export const REGION = MANIFEST.region;
export const TURBULENT = MANIFEST.turbulent;

// limites do plano: [x0, y0, x1, y1]
export const REGION_BOUNDS = REGION.bounds_m;
export const TURB_BOUNDS = TURBULENT.bounds_m;
export const REGION_HALF = (REGION_BOUNDS[2] - REGION_BOUNDS[0]) / 2;
export const TURB_HALF = (TURB_BOUNDS[2] - TURB_BOUNDS[0]) / 2;

export const REGION_ANCHORS = REGION.anchors;
export const TURB_ANCHORS = TURBULENT.anchors;
export const REGION_ROUTES = REGION.routes;
export const TURB_ROUTES = TURBULENT.routes;
export const PLACEMENTS = REGION.placements;
export const TURB_PLACEMENTS = TURBULENT.placements;

export function anchor(id, scene = 'region') {
  const table = scene === 'turbulent' ? TURB_ANCHORS : REGION_ANCHORS;
  const a = table[id];
  if (!a) throw new Error(`âncora desconhecida no manifesto do mundo: ${scene}/${id}`);
  return a;
}
export function route(id, scene = 'region') {
  const table = scene === 'turbulent' ? TURB_ROUTES : REGION_ROUTES;
  const r = table[id];
  if (!r) throw new Error(`rota desconhecida no manifesto do mundo: ${scene}/${id}`);
  return r;
}
export const placementsOfKind = (kind, scene = 'region') =>
  (scene === 'turbulent' ? TURB_PLACEMENTS : PLACEMENTS).filter((p) => p.kind === kind);
export const placement = (name, scene = 'region') =>
  (scene === 'turbulent' ? TURB_PLACEMENTS : PLACEMENTS).find((p) => p.name === name) || null;

// Confere na inicialização que o pacote de runtime é o esperado por este código.
export function assertRuntimeSchema() {
  const major = String(RUNTIME_SCHEMA).split('.')[0];
  if (major !== '1') throw new Error(`pacote de mundo com runtime_schema ${RUNTIME_SCHEMA}; esperado 1.x`);
  if (!REGION.height || !TURBULENT.height) throw new Error('pacote de mundo sem heightfields');
  return true;
}
