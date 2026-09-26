// Heightfields determinísticos do mundo preparado: leitura direta dos binários, amostragem
// bilinear, sem alocação por consulta e sem nenhum raycast contra a geometria.
//
//   região      1025 × 1025 amostras, 1 m, plano x,y ∈ [-512, 512]
//   Turbulenta   513 ×  513 amostras, 0,5 m, plano x,y ∈ [-128, 128]
//
// As consultas recebem coordenadas da cena (x, z do three.js) e convertem internamente pelo
// contrato de `coordinates.js`. Fora dos limites o valor é o da borda (clamp), nunca NaN.
import regionHeightBin from '../../assets/world-runtime/region-height.bin';
import regionMaskBin from '../../assets/world-runtime/region-mask.bin';
import turbulentHeightBin from '../../assets/world-runtime/turbulent-height.bin';
import { REGION, TURBULENT } from './runtime-manifest.js';
import { PLAN_Z_SIGN, TURB_RUNTIME_OFFSET, routeToThree } from './coordinates.js';
import { carveRiver } from './river.js';

const u16From = (bin) => {
  // O carregador do esbuild entrega Uint8Array; a cópia alinha o buffer para a visão de 16 bits.
  const copy = new Uint8Array(bin.byteLength);
  copy.set(bin);
  return new Uint16Array(copy.buffer);
};

function makeField(meta, u16) {
  const [w, h] = meta.samples;
  if (u16.length !== w * h) throw new Error(`heightfield ${meta.file}: ${u16.length} amostras, esperado ${w * h}`);
  const step = meta.step_m;
  return {
    data: u16, w, h, step,
    ox: meta.origin_m[0], oy: meta.origin_m[1],
    min: meta.min, scale: (meta.max - meta.min) / 65535,
    x0: meta.origin_m[0], y0: meta.origin_m[1],
    x1: meta.origin_m[0] + (w - 1) * step, y1: meta.origin_m[1] + (h - 1) * step,
  };
}

export const REGION_FIELD = makeField(REGION.height, u16From(regionHeightBin));
export const TURB_FIELD = makeField(TURBULENT.height, u16From(turbulentHeightBin));

// máscara: 2 amostras por byte (nibble baixo primeiro); bits 0-2 bioma, bit 3 água
const MASK = regionMaskBin;
const MASK_W = REGION.mask.samples[0];
export const BIOME_WATER = REGION.mask.water_biome_index;

// ---------------------------------------------------------------- amostragem
// Bilinear com a mesma diagonal (a-d) usada pela malha do terreno, para que o chão calculado e o
// chão desenhado coincidam vértice a vértice.
function sampleField(f, px, py) {
  const fx = (px - f.x0) / f.step;
  const fy = (py - f.y0) / f.step;
  let i = fx < 0 ? 0 : fx > f.w - 2 ? f.w - 2 : Math.floor(fx);
  let j = fy < 0 ? 0 : fy > f.h - 2 ? f.h - 2 : Math.floor(fy);
  let tx = fx - i, ty = fy - j;
  tx = tx < 0 ? 0 : tx > 1 ? 1 : tx;
  ty = ty < 0 ? 0 : ty > 1 ? 1 : ty;
  const k = j * f.w + i;
  const a = f.data[k], b = f.data[k + 1], c = f.data[k + f.w], d = f.data[k + f.w + 1];
  const u = tx > ty ? a + (b - a) * tx + (d - b) * ty : a + (d - c) * tx + (c - a) * ty;
  return f.min + u * f.scale;
}

// Altura exata em um nó da grade (usada pela construção da malha; sem interpolação).
export function fieldAt(f, i, j) {
  const ci = i < 0 ? 0 : i > f.w - 1 ? f.w - 1 : i;
  const cj = j < 0 ? 0 : j > f.h - 1 ? f.h - 1 : j;
  return f.min + f.data[cj * f.w + ci] * f.scale;
}

// ---------------------------------------------------------------- consultas da cena
export function regionHeightAt(x, z) {
  return sampleField(REGION_FIELD, x, PLAN_Z_SIGN * z);
}
export function turbulentHeightAt(x, z) {
  return sampleField(TURB_FIELD, x - TURB_RUNTIME_OFFSET.x, PLAN_Z_SIGN * (z - TURB_RUNTIME_OFFSET.z)) + TURB_RUNTIME_OFFSET.y;
}

function maskNibble(px, py) {
  const i = Math.round(px - REGION_FIELD.x0);
  const j = Math.round(py - REGION_FIELD.y0);
  if (i < 0 || j < 0 || i >= MASK_W || j >= MASK_W) return 0;
  const k = j * MASK_W + i;
  const byte = MASK[k >> 1];
  return (k & 1) ? (byte >> 4) & 0x0f : byte & 0x0f;
}
export function biomeAt(x, z) { return maskNibble(x, PLAN_Z_SIGN * z) & 7; }
export function isWaterCell(x, z) { return (maskNibble(x, PLAN_Z_SIGN * z) & 8) !== 0; }

// ---------------------------------------------------------------- rio
// O rio da fonte é uma faixa reta de largura fixa; `river.js` refaz o curso dentro do próprio vale,
// escrevendo sobre estas mesmas alturas e máscara antes de qualquer consulta. Ver o cabeçalho de lá.
export const RIVER = carveRiver({
  field: REGION_FIELD, mask: MASK, maskW: MASK_W, placements: REGION.placements,
  routes: Object.values(REGION.routes).map((r) => ({ width: r.width_m, pts: routeToThree(r.points) })),
});
export const WATER_LEVEL = REGION.water.level_m;

const riverSample = (arr, x) => {
  const t = x - RIVER.x0;
  if (t <= 0) return arr[0];
  if (t >= RIVER.n - 1) return arr[RIVER.n - 1];
  const i = Math.floor(t);
  return arr[i] + (arr[i + 1] - arr[i]) * (t - i);
};
// Nível da lâmina na coluna x (o rio atravessa a região de oeste a leste, sem voltar sobre si).
export const waterLevelAt = (x) => riverSample(RIVER.level, x);
// Eixo e meia-largura do canal na coluna x.
export const riverCenterAt = (x) => riverSample(RIVER.center, x);
export const riverHalfWidthAt = (x) => riverSample(RIVER.half, x);
// Umidade do chão (0 seco … 1 encharcado): margens logo acima da água e baixadas do leito antigo.
export function wetnessAt(x, z) {
  const i = Math.round(x - REGION_FIELD.x0), j = Math.round(PLAN_Z_SIGN * z - REGION_FIELD.y0);
  if (i < 0 || j < 0 || i >= RIVER.wetW || j >= RIVER.wetW) return 0;
  return RIVER.wet[j * RIVER.wetW + i] / 255;
}

// Profundidade submersa em um ponto (0 fora do rio).
export function waterDepthAt(x, z) {
  if (!isWaterCell(x, z)) return 0;
  return Math.max(0, waterLevelAt(x) - regionHeightAt(x, z));
}

// ---------------------------------------------------------------- utilidades
export const REGION_LIMITS = {
  x0: REGION_FIELD.x0, x1: REGION_FIELD.x1,
  z0: PLAN_Z_SIGN * REGION_FIELD.y1, z1: PLAN_Z_SIGN * REGION_FIELD.y0,
};
export const TURB_LIMITS = {
  x0: TURB_FIELD.x0 + TURB_RUNTIME_OFFSET.x, x1: TURB_FIELD.x1 + TURB_RUNTIME_OFFSET.x,
  z0: PLAN_Z_SIGN * TURB_FIELD.y1 + TURB_RUNTIME_OFFSET.z, z1: PLAN_Z_SIGN * TURB_FIELD.y0 + TURB_RUNTIME_OFFSET.z,
};

// Inclinação local aproximada (m por m), sem alocação.
export function slopeAt(x, z, d = 1) {
  const h = regionHeightAt(x, z);
  return Math.abs(regionHeightAt(x + d, z) - h) + Math.abs(regionHeightAt(x, z + d) - h);
}
