// Campos naturais da vegetação: onde a mata fecha, onde abre em campo, onde a grama forma manchas
// e para que lado puxa o tom das copas. Funções puras de (x, z), sem estado e sem alocação: a
// floresta, a cobertura do chão e a pintura do terreno leem os mesmos números.
//
// Nenhum destes campos conhece as áreas funcionais; quem planta combina os dois.
import { biomeAt, regionHeightAt, wetnessAt, isWaterCell } from './heightfield.js';
import { LOC } from '../game/layout.js';
import { fbm2, vnoise, smoothstep } from './noise.js';

// Base de mata por bioma: 0 baixada · 1 encostas · 2 rocha/altitude · 3 mata úmida · 4 planalto seco · 5 água
export const BIOME_FOREST = [0.74, 0.72, 0.42, 0.88, 0.56, 0];

// Distorção de domínio: as manchas deixam de ser bolhas redondas e ganham bordas recortadas.
function warp(x, z, scale, amount, seed) {
  return [
    x + amount * (fbm2(x / scale + 3.1, z / scale, 2, seed) - 0.5) * 2,
    z + amount * (fbm2(x / scale, z / scale + 7.7, 2, seed + 1) - 0.5) * 2,
  ];
}

export function inWastes(x, z) {
  const e = LOC.ermos;
  const ex = (x - e.x) / e.rx, ez = (z - e.z) / e.rz;
  return ex * ex + ez * ez < 1;
}

export function slopeHere(x, z) {
  const h = regionHeightAt(x, z);
  return (Math.abs(regionHeightAt(x + 1.5, z) - h) + Math.abs(regionHeightAt(x, z + 1.5) - h)) / 1.5;
}

// Densidade natural de mata, 0 (campo aberto) … 1 (núcleo fechado).
//   ~210 m  massas de floresta e campos abertos
//   ~55 m   bordas irregulares e línguas de mata
//   ~38 m   clareiras de 15–35 m dentro da mata
//   ~14 m   falhas pequenas entre as copas
function forestDensityRaw(x, z) {
  if (isWaterCell(x, z)) return 0;
  const b = biomeAt(x, z);
  const [wx, wz] = warp(x, z, 95, 26, 5);
  const macro = fbm2(wx / 210, wz / 210, 3, 7);
  const meso = fbm2(wx / 55, wz / 55, 3, 8);
  let f = BIOME_FOREST[b] + (macro - 0.5) * 2.0 + (meso - 0.5) * 1.6;
  const gap = vnoise(x / 14, z / 14, 9);
  if (gap < 0.16) f -= (0.16 - gap) * 4;
  const h = regionHeightAt(x, z);
  if (h > 96) f -= (h - 96) / 26;                       // cumes pelados
  f -= smoothstep(0.8, 1.3, slopeHere(x, z)) * 0.55;    // paredões
  f -= wetnessAt(x, z) * 0.45;                           // margem do rio fica aberta, com arbustos
  const bq = LOC.bosque;
  if (bq) f += 0.38 * (1 - smoothstep(bq.r * 0.55, bq.r * 1.35, Math.hypot(x - bq.x, z - bq.z)));
  if (inWastes(x, z)) f = Math.min(f, 0.34);
  let F = smoothstep(0.36, 0.72, f);
  const glade = vnoise(wx / 38, wz / 38, 19);
  if (glade < 0.19) F *= smoothstep(0.07, 0.19, glade);
  return F;
}

// A densidade é suave em escalas de 14 m para cima: uma grade de 2 m, calculada uma vez, responde
// ao terreno, ao planejamento da floresta e à cobertura do chão sem repetir o ruído a cada consulta.
const FG_STEP = 2, FG_N = 513;
let FG = null;
export function forestDensity(x, z) {
  if (!FG) {
    FG = new Float32Array(FG_N * FG_N);
    for (let j = 0; j < FG_N; j++) for (let i = 0; i < FG_N; i++) FG[j * FG_N + i] = forestDensityRaw(-512 + i * FG_STEP, -512 + j * FG_STEP);
  }
  if (isWaterCell(x, z)) return 0;
  const fx = Math.min(FG_N - 1.001, Math.max(0, (x + 512) / FG_STEP)), fz = Math.min(FG_N - 1.001, Math.max(0, (z + 512) / FG_STEP));
  const i = Math.floor(fx), j = Math.floor(fz), tx = fx - i, tz = fz - j;
  const k = j * FG_N + i;
  const a = FG[k], b = FG[k + 1], c = FG[k + FG_N], d = FG[k + FG_N + 1];
  return (a + (b - a) * tx) * (1 - tz) + (c + (d - c) * tx) * tz;
}

// Touceiras dentro da mata: 0,55 (trecho ralo) … 1 (touceira fechada), em manchas de ~28 m.
export function standClump(x, z) {
  return 0.55 + 0.45 * smoothstep(0.36, 0.64, fbm2(x / 28 + 1.7, z / 28 - 3.3, 2, 29));
}

// Tom das copas: 0 verde-azulado … 1 verde-amarelado, em manchas de ~120 m.
export function canopyHue(x, z) {
  return smoothstep(0.32, 0.68, fbm2(x / 120 + 11.3, z / 120 - 4.2, 2, 13));
}

// Idade da mata (0 jovem … 1 madura), em manchas de ~40 m: puxa a escala das árvores.
export function standAge(x, z) {
  return fbm2(x / 40 - 2.7, z / 40 + 5.1, 2, 17);
}

// Campo de manchas da grama, [0, 1]: domínio distorcido em três escalas (~7, ~23 e ~61 m).
export function grassPatch(x, z) {
  const [wx, wz] = warp(x, z, 18, 7, 21);
  return 0.5 * fbm2(wx / 7, wz / 7, 2, 23) + 0.35 * fbm2(wx / 23, wz / 23, 2, 25) + 0.15 * fbm2(wx / 61, wz / 61, 2, 27);
}
