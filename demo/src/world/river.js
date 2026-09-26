// Rio principal cavado no próprio vale.
//
// A exportação do Blender traz o rio como uma faixa de 17 m de largura constante, quase reta, sobre
// um V aberto em linha reta no relevo. Aqui o curso é refeito uma única vez, ao carregar o
// heightfield: o centro serpenteia dentro do vale, a largura e a profundidade variam, a margem
// externa das curvas vira barranco e a interna vira praia de cascalho, e o terreno logo fora da água
// fica sempre acima da lâmina. Os trechos do leito antigo que ficam para trás viram baixadas úmidas.
//
// Tudo é escrito sobre os próprios dados do heightfield e da máscara, então o chão desenhado, o chão
// do jogo (`groundHeight`), a água do jogo (`waterAt`), o mapa, a coleta e os testes enxergam o mesmo
// rio. Estradas, pontes e construções limitam o traçado: nada é cavado sobre uma rota fora do
// tabuleiro de uma ponte, e a água nunca chega a menos de 3 m de uma construção.
import { PLAN_Z_SIGN } from './coordinates.js';
import { vnoise, vnoise1, fbm2, blur1, smoothstep, lerp } from './noise.js';

const ROUTE_EDGE = 0.6;    // folga entre a borda da água e a borda da estrada
const BUILD_EDGE = 3;      // folga entre a água e qualquer construção
const VALLEY = 14;         // quanto o centro pode se afastar do fundo do vale original
const W_MIN = 2.8;

// suavização polinomial do mínimo (sem vinco onde o corte encontra o relevo)
const smin = (a, b, k) => {
  const h = Math.max(k - Math.abs(a - b), 0) / k;
  return Math.min(a, b) - h * h * k * 0.25;
};
const easeOut = (t) => 1 - (1 - t) * (1 - t);

// Densifica uma polilinha da cena ([x, z], …) a cada `seg` metros.
function samples(pts, seg) {
  const out = [];
  for (let i = 0; i < pts.length - 1; i++) {
    const [ax, az] = pts[i], [bx, bz] = pts[i + 1];
    const n = Math.max(1, Math.ceil(Math.hypot(bx - ax, bz - az) / seg));
    for (let s = 0; s < n; s++) out.push([ax + (bx - ax) * s / n, az + (bz - az) * s / n]);
  }
  out.push(pts[pts.length - 1]);
  return out;
}

export function carveRiver({ field, mask, maskW, placements, routes }) {
  const W = field.w, N = W;                       // colunas de 1 m, x = i + x0
  const x0 = field.x0, y0 = field.y0;
  const toJ = (z) => Math.round(PLAN_Z_SIGN * z - y0);
  const toZ = (j) => PLAN_Z_SIGN * (j + y0);
  const H = (i, j) => field.min + field.data[j * W + i] * field.scale;
  const nib = (k) => { const b = mask[k >> 1]; return (k & 1) ? (b >> 4) & 15 : b & 15; };
  const setNib = (k, v) => {
    const b = mask[k >> 1];
    mask[k >> 1] = (k & 1) ? (b & 0x0f) | (v << 4) : (b & 0xf0) | v;
  };

  // ------------------------------------------------------------ 1. o rio da fonte, coluna a coluna
  const c0 = new Float64Array(N), landA = new Uint8Array(N), landB = new Uint8Array(N);
  const oldLo = new Int32Array(N).fill(-1), oldHi = new Int32Array(N).fill(-1);
  for (let i = 0; i < N; i++) {
    for (let j = 0; j < field.h; j++) {
      if (!(nib(j * maskW + i) & 8)) continue;
      if (oldLo[i] < 0) oldLo[i] = j;
      oldHi[i] = j;
    }
    if (oldLo[i] >= 0) {
      c0[i] = toZ((oldLo[i] + oldHi[i]) / 2);
      landA[i] = nib(Math.max(0, oldLo[i] - 2) * maskW + i) & 7;
      landB[i] = nib(Math.min(field.h - 1, oldHi[i] + 2) * maskW + i) & 7;
    } else c0[i] = NaN;
  }
  // colunas sem água (não há hoje) herdam o vizinho
  for (let i = 0; i < N; i++) if (Number.isNaN(c0[i])) c0[i] = i ? c0[i - 1] : 0;
  for (let i = N - 1; i >= 0; i--) if (Number.isNaN(c0[i])) c0[i] = c0[i + 1];
  blur1(c0, 3);

  // fundo do vale: mínimo a até 8 m do eixo original, suavizado → nível da lâmina
  const level = new Float64Array(N);
  for (let i = 0; i < N; i++) {
    const jc = toJ(c0[i]);
    let lo = Infinity;
    for (let j = Math.max(0, jc - 8); j <= Math.min(field.h - 1, jc + 8); j++) lo = Math.min(lo, H(i, j));
    level[i] = lo;
  }
  blur1(level, 16);
  for (let i = 0; i < N; i++) level[i] += 0.35;

  // ------------------------------------------------------------ 2. pontes, estradas e construções
  const bridges = placements.filter((p) => p.bridge).map((p) => ({
    x: p.plan[0], z: PLAN_Z_SIGN * p.plan[1], hw: p.half[0], hd: p.half[1],
  }));
  const buildings = placements.filter((p) => !p.bridge && p.kind !== 'field').map((p) => ({
    x: p.plan[0], z: PLAN_Z_SIGN * p.plan[1], hw: p.half[0], hd: p.half[1],
  }));
  const onDeck = (x, z, padX = 0, padZ = padX) =>
    bridges.some((b) => Math.abs(x - b.x) <= b.hw + padX && Math.abs(z - b.z) <= b.hd + padZ);

  // amostras das rotas a cada 1 m, agrupadas por coluna. As que estão sobre o tabuleiro, no começo
  // da rampa ou saindo pela lateral da ponte (apoiadas no encontro) não limitam o canal: a trilha
  // da mina, por exemplo, deixa o tabuleiro pelo lado e desce rente à água.
  // Para proteger a pista (e mantê-la fora da água) valem todas as amostras fora do tabuleiro.
  const byCol = new Map(), byColAll = new Map();
  const put = (map, c, v) => { if (!map.has(c)) map.set(c, []); map.get(c).push(v); };
  for (const r of routes) {
    for (const [x, z] of samples(r.pts, 1)) {
      const c = Math.round(x - x0), v = { x, z, half: r.width / 2 };
      if (!onDeck(x, z, 0.5, 0.5)) put(byColAll, c, v);
      if (!onDeck(x, z, 12, 3)) put(byCol, c, v);
    }
  }
  const nearIn = (map, i, reach) => {
    const out = [];
    for (let c = i - reach; c <= i + reach; c++) { const l = map.get(c); if (l) out.push(...l); }
    return out;
  };
  const near = (i, reach) => nearIn(byCol, i, reach);

  // ------------------------------------------------------------ 3. traçado: meandros dentro do vale
  const pin = new Float64Array(N).fill(1);       // 0 no tabuleiro, 1 longe das pontes
  const base = Float64Array.from(c0);
  for (const b of bridges) {
    const ib = Math.round(b.x - x0);
    for (let i = 0; i < N; i++) {
      const s = smoothstep(b.hw + 6, b.hw + 46, Math.abs(i + x0 - b.x));
      pin[i] = Math.min(pin[i], s);
      base[i] += (b.z - c0[ib]) * (1 - s);
    }
  }
  const half = new Float64Array(N), depth = new Float64Array(N), want = new Float64Array(N);
  for (let i = 0; i < N; i++) {
    const x = i + x0;
    const amp = 5 + 7 * vnoise1(x / 260, 11);
    const wave = 0.6 * Math.sin(2 * Math.PI * x / 173 + 1.3) + 0.3 * Math.sin(2 * Math.PI * x / 97 + 4.1)
      + 0.1 * Math.sin(2 * Math.PI * x / 53 + 2.2);
    want[i] = base[i] + amp * wave * pin[i];
    half[i] = 3.5 + 5.5 * vnoise1(x / 70, 23);
    depth[i] = 0.55 + 0.4 * vnoise1(x / 90, 37);
    for (const b of bridges) {
      const s = smoothstep(b.hw + 2, b.hw + 30, Math.abs(x - b.x));
      half[i] = Math.min(half[i], lerp(b.hd - 2.5, 99, s));
    }
    half[i] = Math.max(W_MIN, half[i]);
  }
  blur1(half, 4);

  // Centros permitidos numa coluna: dentro do vale, dentro do vão da ponte e longe de estradas e
  // construções. Devolve o permitido mais perto do desejado, ou null.
  const reachCols = 16;
  function allowed(i, cz, w) {
    const x = i + x0;
    let lo = base[i] - VALLEY, hi = base[i] + VALLEY;
    for (const b of bridges) {
      if (Math.abs(x - b.x) > b.hw + 2) continue;
      const slack = Math.max(0, b.hd - 2.5 - w);
      lo = Math.max(lo, b.z - slack); hi = Math.min(hi, b.z + slack);
    }
    if (lo > hi) return null;
    const bad = [];
    const reach = w + 2.5;                       // água mais o barranco
    for (const s of near(i, reachCols)) {
      const clr = s.half + ROUTE_EDGE + 1.4;
      const dx = Math.abs(s.x - x);
      if (dx >= clr) continue;
      const r = Math.sqrt(clr * clr - dx * dx);
      bad.push([s.z - r - reach, s.z + r + reach]);
    }
    for (const b of buildings) {
      const dx = Math.max(0, Math.abs(x - b.x) - b.hw);
      if (dx >= BUILD_EDGE + reach) continue;
      bad.push([b.z - b.hd - BUILD_EDGE - reach, b.z + b.hd + BUILD_EDGE + reach]);
    }
    // candidatos: o desejado e as bordas de cada intervalo proibido
    const cands = [Math.min(hi, Math.max(lo, cz))];
    for (const [a, b] of bad) { cands.push(a - 0.01, b + 0.01); }
    let best = null, bestD = Infinity;
    for (const c of cands) {
      if (c < lo || c > hi) continue;
      if (bad.some(([a, b]) => c > a && c < b)) continue;
      const d = Math.abs(c - cz);
      if (d < bestD) { bestD = d; best = c; }
    }
    return best;
  }

  const cz = new Float64Array(N);
  let constrained = 0;
  const solve = (src) => {
    for (let i = 0; i < N; i++) {
      let w = half[i], c = allowed(i, src[i], w);
      while (c === null && w > W_MIN + 0.05) { w = Math.max(W_MIN, w * 0.8); c = allowed(i, src[i], w); }
      if (c === null) { constrained++; c = src[i]; }
      half[i] = w; cz[i] = c;
    }
  };
  solve(want);
  blur1(cz, 6);
  constrained = 0;
  solve(Float64Array.from(cz));
  blur1(cz, 4);
  // curvas sem degrau: no máximo 0,6 m de desvio lateral por metro de rio
  for (let pass = 0; pass < 2; pass++) {
    for (let i = 1; i < N; i++) cz[i] = Math.min(cz[i - 1] + 0.6, Math.max(cz[i - 1] - 0.6, cz[i]));
    for (let i = N - 2; i >= 0; i--) cz[i] = Math.min(cz[i + 1] + 0.6, Math.max(cz[i + 1] - 0.6, cz[i]));
  }

  // ------------------------------------------------------------ 4. escavação
  const wet = new Uint8Array(W * field.h);
  const oldWater = [];
  for (let i = 0; i < N; i++) if (oldLo[i] >= 0) for (let j = oldLo[i]; j <= oldHi[i]; j++) {
    const k = j * maskW + i;
    if (nib(k) & 8) oldWater.push(k);
  }
  const newWater = [];
  const q = (h) => Math.max(0, Math.min(65535, Math.round((h - field.min) / field.scale)));

  for (let i = 0; i < N; i++) {
    const x = i + x0;
    const im = Math.max(0, i - 1), ip = Math.min(N - 1, i + 1);
    const slope = (cz[ip] - cz[im]) / (ip - im);
    const kap = cz[ip] - 2 * cz[i] + cz[im];
    const strength = Math.min(1, Math.abs(kap) / 0.012);
    const outerSign = kap < 0 ? 1 : -1;          // lado +s é o externo quando a curva abre para -z
    const asym = outerSign * 0.35 * strength;
    const norm = 1 / Math.sqrt(1 + slope * slope);
    const L = level[i], d = depth[i] + 0.25 * strength;
    const zLo = Math.min(cz[i], c0[i]) - 32, zHi = Math.max(cz[i], c0[i]) + 32;
    const ja = Math.max(0, Math.min(toJ(zLo), toJ(zHi))), jb = Math.min(field.h - 1, Math.max(toJ(zLo), toJ(zHi)));
    const nearR = nearIn(byColAll, i, 8);

    for (let j = ja; j <= jb; j++) {
      const z = toZ(j);
      const s = (z - cz[i]) * norm;
      const wEff = half[i] + 0.6 * (fbm2(x / 6, z / 6, 2, 31) * 2 - 1);
      const a = Math.abs(s) - wEff;
      const h0 = H(i, j);

      // proteção: estrada fora do tabuleiro e construções ficam como estão
      let p = 1, onRoad = Infinity;
      if (!onDeck(x, z)) {
        for (const r of nearR) {
          const e = Math.hypot(r.x - x, r.z - z) - r.half;
          if (e < onRoad) onRoad = e;
          if (e < 3) p = Math.min(p, smoothstep(ROUTE_EDGE, 3, e));
        }
      }
      for (const b of buildings) {
        const e = Math.hypot(Math.max(0, Math.abs(x - b.x) - b.hw), Math.max(0, Math.abs(z - b.z) - b.hd));
        if (e < BUILD_EDGE + 2) p = Math.min(p, smoothstep(1, BUILD_EDGE + 2, e));
      }

      let h = h0;
      if (a <= 0) {
        const u = s / wEff;
        const bed = L - 0.05 - (d - 0.05) * (1 - u * u) * (1 + asym * u);
        h = Math.min(h0, bed);
      } else {
        const outer = Math.sign(s) === Math.sign(asym) && strength > 0.15;
        const bw = outer ? lerp(2.4, 1.6, strength) : lerp(3.5, 5, strength);
        const fb = 0.3 + 0.3 * vnoise(x / 9, z / 9, 41);
        const floorH = L + 0.03 + (fb - 0.03) * easeOut(Math.min(1, a / bw));
        const ceilH = floorH + (outer ? 0.85 : 0.35) * Math.max(0, a - 0.15);
        const cutW = 1 - smoothstep(bw + 3, bw + 9, a);
        const fillW = 1 - smoothstep(bw, bw + 1.5, a);
        if (h > ceilH && cutW > 0) h = lerp(h, smin(h, ceilH, 0.6), cutW);
        if (h < floorH && fillW > 0) h = lerp(h, floorH, fillW);
      }
      h = lerp(h0, h, p);
      // a pista nunca fica submersa: onde a fonte a traçou pelo leito (a trilha da mina sai da ponte
      // pela lateral), ela vira um aterro rente à água
      if (onRoad < 0.6) h = Math.max(h, L + 0.12 * smoothstep(0.6, 0, onRoad) + 0.02);
      if (h !== h0) field.data[j * W + i] = q(h);
      const hf = field.min + field.data[j * W + i] * field.scale;

      const k = j * maskW + i;
      if (a <= 0 && hf < L - 0.02) newWater.push(k);
      // umidade: margem logo acima da água e baixadas do leito antigo
      let wv = 0;
      if (a <= 0) wv = 1;
      else {
        const reach = 3.5 + 3.5 * vnoise(x / 13, z / 13, 53);
        wv = 1 - smoothstep(0, reach, a);
        if (hf < L + 0.3) wv = Math.max(wv, 0.95);
        else if (hf < L + 1.1 && vnoise(x / 11, z / 11, 59) > 0.62) wv = Math.max(wv, 0.7);
      }
      if (wv > 0) wet[k] = Math.max(wet[k], Math.round(wv * 255));
    }
  }

  // ------------------------------------------------------------ 5. máscara: água só no novo canal
  for (const k of oldWater) {
    const i = k % maskW, j = (k - i) / maskW;
    const mid = (oldLo[i] + oldHi[i]) / 2;
    setNib(k, j < mid ? landA[i] : landB[i]);
  }
  for (const k of newWater) setNib(k, 8 | 5);

  // ------------------------------------------------------------ 6. relatório
  let bends = 0, maxOff = 0, minW = Infinity, maxW = 0;
  let prev = 0;
  for (let i = 1; i < N - 1; i++) {
    const off = cz[i] - c0[i];
    maxOff = Math.max(maxOff, Math.abs(off));
    const k2 = cz[i + 1] - 2 * cz[i] + cz[i - 1];
    const sg = Math.abs(k2) < 2e-4 ? 0 : Math.sign(k2);
    if (sg && prev && sg !== prev) bends++;
    if (sg) prev = sg;
    minW = Math.min(minW, half[i]); maxW = Math.max(maxW, half[i]);
  }

  return {
    x0, n: N,
    center: Float32Array.from(cz), original: Float32Array.from(c0),
    half: Float32Array.from(half), level: Float32Array.from(level), depth: Float32Array.from(depth),
    wet, wetW: maskW,
    stats: {
      bends, maxOffset: +maxOff.toFixed(2), minHalf: +minW.toFixed(2), maxHalf: +maxW.toFixed(2),
      constrained, waterCells: newWater.length, releasedCells: oldWater.length,
    },
  };
}
