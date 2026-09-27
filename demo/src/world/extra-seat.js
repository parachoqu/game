// Contas puras dos assets extras: onde cada modelo fica, em que escala, sobre que chão e que pegada
// ele cobre. Servem ao runtime (`extra-props.js`), às clareiras da vegetação (`functional-areas.js`)
// e aos testes — a mesma conta nos três, sem objetos do three.js.
//
// Três ideias ficam separadas:
//   pegada visual   contorno da base do modelo (manifesto `extra.footprint`, unidades do modelo),
//                   levado para a cena pela escala, giro e posição finais;
//   colisor         o do manifesto do mundo (trocas) ou o do acréscimo — nada aqui o muda;
//   clareira        núcleo sem grama = pegada + margem curta (`functional-areas.js`).
import { EXTRA_META } from '../engine/extra-assets.js';
import { PLACEMENTS } from './runtime-manifest.js';
import { PLAN_Z_SIGN } from './coordinates.js';
import { regionHeightAt } from './heightfield.js';
import { WORLD_BRIDGES, WORLD_COLLIDERS } from './world-colliders.js';

// Trocas só visuais: o volume do Blender some e o modelo novo entra na mesma caixa.
//   'caixa'   escala uniforme para caber na pegada (sem passar de 1,6× a altura antiga), frente para
//             +Z — as caixas _Facade e _Rear do Blender têm o mesmo centro — ou para `yaw`
//   'redonda' o diâmetro do modelo vira o diâmetro do colisor redondo
//   'ponte'   o vão de pedra sem deformar, e por cima o tabuleiro plano (`bridge-roadway.js`)
//   'doca'    escala da pegada; o piso de tábuas na altura do piso do cais antigo (`deck`)
// `tint` pinta o telhado das bancas com o tecido que o mercado já usava.
export const REPLACEMENTS = [
  { asset: 'ponte_pedra', volume: 'Bridge_Main', fit: 'ponte' },
  { asset: 'ferraria', volume: 'SouthWorkshop', fit: 'caixa' },
  { asset: 'casa_palha', volume: 'NorthHouse_01', fit: 'caixa' },
  { asset: 'casa_palha', volume: 'NorthHouse_02', fit: 'caixa' },
  { asset: 'casa_palha', volume: 'NorthHouse_03', fit: 'caixa' },
  { asset: 'torre_a', volume: 'SouthOutpost_Tower', fit: 'redonda' },
  { asset: 'torre_b', volume: 'NorthOutpost_Tower', fit: 'redonda' },
  // Vale: casas, armazém, estábulo, mercado e doca
  { asset: 'casa_s32_b', volume: 'SouthHouse_01', fit: 'caixa' },
  { asset: 'casa_s32_c', volume: 'SouthHouse_02', fit: 'caixa' },
  { asset: 'casa_pedra', volume: 'SouthHouse_03', fit: 'caixa' },
  { asset: 'casa_s32_a', volume: 'SouthHouse_04', fit: 'caixa' },
  { asset: 'armazem', volume: 'SouthWarehouse', fit: 'caixa' },
  { asset: 'casa_palha', volume: 'SouthStable', fit: 'caixa', yaw: -Math.PI / 2 },   // porta para oeste, onde fica o cuidador
  { asset: 'banca', volume: 'SouthMarket_01', fit: 'caixa', yaw: 0, tint: '#b24a3a' },   // PR.COL.tecido1
  { asset: 'banca', volume: 'SouthMarket_02', fit: 'caixa', yaw: 0, tint: '#3e7a9a' },   // PR.COL.tecido3
  { asset: 'doca', volume: 'SouthOutpost_Dock', fit: 'doca', yaw: 0, deck: 11 },   // topo das tábuas do cais antigo
  { asset: 'ponte_pedra', volume: 'Bridge_Minor', fit: 'ponte' },
  // Ermos: muros do kit Kenney e a torre com escombros
  { asset: 'muro_a', volume: 'Wastes_Ruin_01', fit: 'caixa', yaw: 0 },
  { asset: 'muro_b', volume: 'Wastes_Ruin_02', fit: 'caixa', yaw: 0 },
  { asset: 'torre_escombros', volume: 'Wastes_Ruin_03', fit: 'redonda' },
];

// ---------------------------------------------------------------- transformações
// Giro em Y como no three.js (`rotation.y`): (x, z) → (x cos + z sen, −x sen + z cos).
export const rotY = (yaw, x, z) => [x * Math.cos(yaw) + z * Math.sin(yaw), -x * Math.sin(yaw) + z * Math.cos(yaw)];

// Escala (x, y, z do modelo), giro e centro de uma troca.
export function replacementFit(r) {
  const p = PLACEMENTS.find((q) => q.name === r.volume), meta = EXTRA_META[r.asset];
  if (!p || !meta) return null;
  const x = p.plan[0], z = PLAN_Z_SIGN * p.plan[1], [hw, hd] = p.half;
  const [sx, sy, sz] = meta.size;
  if (r.fit === 'ponte') {
    // comprimento com meio metro a mais em cada ponta; a largura põe a borda da pista na borda do
    // tabuleiro de colisão, para os parapeitos antigos ficarem dentro das paredes novas
    const longX = meta.extra.longAxis === 'x', targetLongZ = hd >= hw;
    const yaw = longX === targetLongZ ? Math.PI / 2 : 0;
    const sLong = (2 * Math.max(hw, hd) + 1) / (longX ? sx : sz);
    const bridge = WORLD_BRIDGES.find((b) => b.name === r.volume);
    const half = bridge ? (targetLongZ ? bridge.hw : bridge.hd) : Math.min(hw, hd);
    const sWide = meta.extra.roadHalf ? half / meta.extra.roadHalf : sLong;
    return { r, p, meta, x, z, yaw, s: longX ? [sLong, sLong, sWide] : [sWide, sLong, sLong] };
  }
  const yaw = r.yaw ?? 0;
  const turned = Math.abs(Math.sin(yaw)) > Math.SQRT1_2;
  const w = turned ? sz : sx, d = turned ? sx : sz;
  let s = r.fit === 'redonda' ? (2 * Math.min(hw, hd)) / Math.max(sx, sz) : Math.min((2 * hw) / w, (2 * hd) / d);
  if (r.fit === 'caixa') s = Math.min(s, (1.6 * (p.top - p.base)) / sy);
  return { r, p, meta, x, z, yaw, s: [s, s, s] };
}

// Acréscimos (`extra-spots.js`): escala 1, exceto a ponte em ruína, que encolhe até a base caber no
// colisor de caixa do ponto (visual, pegada e colisor alinhados; o colisor não muda).
export function additionFit(spot) {
  const meta = EXTRA_META[spot.asset];
  if (!meta) return null;
  let s = 1;
  if (spot.col.box && meta.extra.footprint) {
    const b = boundsOf(meta.extra.footprint);
    s = Math.min(1, spot.col.box[0] / Math.max(-b.x0, b.x1), spot.col.box[1] / Math.max(-b.z0, b.z1));
  }
  return { meta, x: spot.x, z: spot.z, yaw: spot.yaw, s: [s, s, s] };
}

function boundsOf(pts) {
  let x0 = Infinity, x1 = -Infinity, z0 = Infinity, z1 = -Infinity;
  for (const [x, z] of pts) { x0 = Math.min(x0, x); x1 = Math.max(x1, x); z0 = Math.min(z0, z); z1 = Math.max(z1, z); }
  return { x0, x1, z0, z1 };
}

// Pegada da base na cena (polígono convexo, sentido do manifesto).
export function footprintWorld(f) {
  const fp = f.meta.extra.footprint;
  if (!fp) return null;
  return fp.map(([lx, lz]) => {
    const [dx, dz] = rotY(f.yaw, lx * f.s[0], lz * f.s[2]);
    return [f.x + dx, f.z + dz];
  });
}

// ---------------------------------------------------------------- assentamento
// Amostras do chão sob a pegada: cantos, meio das bordas, o polígono encolhido pela metade e o centro.
function groundSamples(poly, cx, cz) {
  const out = [[cx, cz]];
  for (let i = 0; i < poly.length; i++) {
    const [ax, az] = poly[i], [bx, bz] = poly[(i + 1) % poly.length];
    out.push([ax, az], [(ax + bx) / 2, (az + bz) / 2], [cx + (ax - cx) / 2, cz + (az - cz) / 2]);
  }
  return out.map(([x, z]) => regionHeightAt(x, z));
}

export const EMBED = 0.06;   // quanto a base entra no chão onde o terreno é mais alto

// Altura do nó do modelo (a origem dele é a base normalizada) e o que o chão faz sob a pegada.
//   flat   datum = mediana do chão, sem enterrar o lado alto mais que `maxBury`; onde o chão cai
//          abaixo do datum, a base desce até ele (fundação, `drape`)
//   point  tronco: o datum é o chão no centro; as raízes descem até o chão onde ele cai
export function seatOf(f) {
  const ex = f.meta.extra, poly = footprintWorld(f);
  const contact = (ex.contactY || 0) * f.s[1];
  if (!poly || poly.length < 3) {
    const h = regionHeightAt(f.x, f.z);
    return { y: h - EMBED - contact, datum: h, lo: h, hi: h, drape: false };
  }
  const hs = groundSamples(poly, f.x, f.z);
  const sorted = [...hs].sort((a, b) => a - b);
  const lo = sorted[0], hi = sorted.at(-1), med = sorted[Math.floor(sorted.length / 2)];
  let diam = 0;
  for (const a of poly) for (const b of poly) diam = Math.max(diam, Math.hypot(a[0] - b[0], a[1] - b[1]));
  if (ex.support === 'point') {
    const datum = hs[0];
    return { y: datum - 0.15 - contact, datum, lo, hi, drape: lo < datum - 0.2 };
  }
  const maxBury = Math.min(0.5, Math.max(0.15, 0.1 + 0.04 * diam));
  const datum = Math.max(med, hi - maxBury);
  return { y: datum - EMBED - contact, datum, lo, hi, drape: lo < datum - EMBED - 0.03 };
}

// ---------------------------------------------------------------- tabuleiro das pontes
// O tabuleiro plano segue a superfície de caminhada da ponte (a mesma conta de `groundHeight`): o
// piso no tabuleiro e a rampa até o terreno. Vai de uma cabeceira à outra, onde o terreno alcança a
// rampa. Longo = eixo da travessia.
export function bridgeFrame(name) {
  const b = WORLD_BRIDGES.find((q) => q.name === name);
  if (!b) return null;
  const alongZ = b.hd >= b.hw;
  const half = alongZ ? b.hw : b.hd, len = alongZ ? b.hd : b.hw;
  const toWorld = (t, c) => (alongZ ? [b.x + c, b.z + t] : [b.x + t, b.z + c]);
  const walkY = (t, c) => {
    const [x, z] = toWorld(t, c);
    const h = regionHeightAt(x, z);
    const ax = Math.abs(c), az = Math.abs(t);
    if (ax > half + b.apronSide || az > len + b.apron) return h;
    const wx = ax <= half ? 1 : 1 - (ax - half) / b.apronSide;
    const wz = az <= len ? 1 : 1 - (az - len) / b.apron;
    const w = Math.min(wx, wz);
    if (w <= 0) return h;
    const k = w * w * (3 - 2 * w);
    return Math.max(h, h + (b.y - h) * k);
  };
  // cabeceiras: da ponta do tabuleiro para fora, até o terreno encostar na rampa
  const reach = (dir) => {
    for (let t = len; t <= len + b.apron; t += 0.25) {
      const [x, z] = toWorld(dir * t, 0);
      if (regionHeightAt(x, z) >= walkY(dir * t, 0) - 0.1) return t;
    }
    return len + b.apron;
  };
  // segmentos de parapeito do colisor, no referencial da ponte
  const rails = WORLD_COLLIDERS.filter((c) => c.name === `${name}#parapeito`).map((c) => {
    const t = alongZ ? c.z - b.z : c.x - b.x, cc = alongZ ? c.x - b.x : c.z - b.z;
    const ht = alongZ ? c.hd : c.hw, hc = alongZ ? c.hw : c.hd;
    return { t0: t - ht, t1: t + ht, c0: cc - hc, c1: cc + hc };
  });
  return { b, alongZ, half, len, toWorld, walkY, tA: reach(-1), tB: reach(1), rails, wall: 0.6 };
}

// Pegada do tabuleiro (de uma cabeceira à outra, com as paredes) para a clareira da vegetação.
export function roadwayFootprint(name) {
  const f = bridgeFrame(name);
  if (!f) return null;
  const c = f.half + f.wall;
  return [[-f.tA, -c], [f.tB, -c], [f.tB, c], [-f.tA, c]].map(([t, cc]) => f.toWorld(t, cc));
}
