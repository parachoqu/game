// Texturas procedurais geradas ao abrir (nada é baixado): ruído periódico sem emendas, células para pedras,
// normal a partir da altura (Sobel) e rugosidade no canal alfa da normal. Folhagem desenhada em canvas.
import * as THREE from 'three';
import { applyNatureTextures } from './nature-textures.js';

// ---------- ruído periódico ----------
function hash(i, j, s) {
  let h = Math.imul(i | 0, 374761393) ^ Math.imul(j | 0, 668265263) ^ Math.imul(s | 0, 1442695041);
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
}
const wrap = (a, p) => ((a % p) + p) % p;
// ruído de valor periódico com períodos Px, Py (em células)
function vn(x, y, Px, Py, s) {
  const xi = Math.floor(x), yi = Math.floor(y);
  const xf = x - xi, yf = y - yi;
  const u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
  const x0 = wrap(xi, Px), y0 = wrap(yi, Py), x1 = (x0 + 1) % Px, y1 = (y0 + 1) % Py;
  const a = hash(x0, y0, s), b = hash(x1, y0, s), c = hash(x0, y1, s), d = hash(x1, y1, s);
  return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
// fbm periódico em u,v ∈ [0,1): fx, fy = frequências base inteiras (anisotrópico se diferentes)
function fbm(u, v, fx, fy, oct, s, gain = 0.5) {
  let sum = 0, amp = 1, norm = 0;
  for (let o = 0; o < oct; o++) {
    sum += vn(u * fx, v * fy, fx, fy, s + o * 31) * amp;
    norm += amp; amp *= gain; fx *= 2; fy *= 2;
  }
  return sum / norm;
}
const ridge = (u, v, f, oct, s) => { let sum = 0, amp = 1, norm = 0; for (let o = 0; o < oct; o++) { sum += (1 - Math.abs(vn(u * f, v * f, f, f, s + o * 13) * 2 - 1)) * amp; norm += amp; amp *= 0.5; f *= 2; } return sum / norm; };
// Worley periódico: F1, F2 e id da célula mais próxima
const W = { f1: 0, f2: 0, id: 0 };
function worley(u, v, cells, s) {
  const x = u * cells, y = v * cells, xi = Math.floor(x), yi = Math.floor(y);
  let f1 = 9, f2 = 9, id = 0;
  for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) {
    const cx = xi + dx, cy = yi + dy, wx = wrap(cx, cells), wy = wrap(cy, cells);
    const px = cx + hash(wx, wy, s), py = cy + hash(wx, wy, s + 7);
    const d = Math.hypot(px - x, py - y);
    if (d < f1) { f2 = f1; f1 = d; id = hash(wx, wy, s + 13); } else if (d < f2) f2 = d;
  }
  W.f1 = f1; W.f2 = f2; W.id = id;
  return W;
}
const clamp01 = (x) => (x < 0 ? 0 : x > 1 ? 1 : x);
const mix = (a, b, t) => a + (b - a) * t;
const sstep = (a, b, x) => { const t = clamp01((x - a) / (b - a)); return t * t * (3 - 2 * t); };

// ---------- montagem: albedo (RGBA, A = altura) + normal (RGB, A = rugosidade) ----------
const px = { r: 0, g: 0, b: 0, h: 0, rough: 0.9 };
function build(N, fn, strength) {
  const alb = new Uint8Array(N * N * 4), nrm = new Uint8Array(N * N * 4), H = new Float32Array(N * N), R = new Float32Array(N * N);
  for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) {
    px.rough = 0.9; fn(i / N, j / N);
    const k = j * N + i, q = k * 4;
    alb[q] = clamp01(px.r) * 255; alb[q + 1] = clamp01(px.g) * 255; alb[q + 2] = clamp01(px.b) * 255; alb[q + 3] = clamp01(px.h) * 255;
    H[k] = px.h; R[k] = px.rough;
  }
  const s = strength * N / 256;
  for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) {
    const l = H[j * N + wrap(i - 1, N)], r = H[j * N + wrap(i + 1, N)];
    const d = H[wrap(j - 1, N) * N + i], u = H[wrap(j + 1, N) * N + i];
    let nx = (l - r) * s, ny = (d - u) * s, nz = 1;
    const inv = 1 / Math.hypot(nx, ny, nz); nx *= inv; ny *= inv; nz *= inv;
    const q = (j * N + i) * 4;
    nrm[q] = (nx * 0.5 + 0.5) * 255; nrm[q + 1] = (ny * 0.5 + 0.5) * 255; nrm[q + 2] = (nz * 0.5 + 0.5) * 255; nrm[q + 3] = clamp01(R[j * N + i]) * 255;
  }
  return { alb, nrm, N };
}
function set(r, g, b, h, rough) { px.r = r; px.g = g; px.b = b; px.h = h; if (rough !== undefined) px.rough = rough; }

// ---------- camadas do terreno (cores reais) ----------
const TERRAIN = [
  // 0 grama
  (u, v) => {
    const patch = fbm(u, v, 4, 4, 4, 11), blades = fbm(u, v, 96, 24, 3, 21), fine = vn(u * 256, v * 256, 256, 256, 5);
    const t = clamp01(patch * 0.7 + blades * 0.5 - 0.1);
    let r = mix(0.13, 0.36, t), g = mix(0.25, 0.48, t), b = mix(0.07, 0.17, t);
    const dry = sstep(0.62, 0.8, fbm(u, v, 8, 8, 3, 41)) * 0.5;
    r = mix(r, 0.46, dry); g = mix(g, 0.44, dry); b = mix(b, 0.22, dry);
    const soil = sstep(0.72, 0.9, 1 - patch) * 0.6;
    r = mix(r, 0.3, soil); g = mix(g, 0.24, soil); b = mix(b, 0.15, soil);
    const f = 0.9 + fine * 0.2;
    set(r * f, g * f, b * f, blades * 0.7 + fine * 0.3, 0.92);
  },
  // 1 grama seca
  (u, v) => {
    const patch = fbm(u, v, 4, 4, 4, 12), blades = fbm(u, v, 96, 20, 3, 22), fine = vn(u * 256, v * 256, 256, 256, 6);
    const t = clamp01(patch * 0.6 + blades * 0.6 - 0.1);
    const f = 0.88 + fine * 0.24;
    set(mix(0.38, 0.66, t) * f, mix(0.33, 0.58, t) * f, mix(0.16, 0.3, t) * f, blades * 0.7 + fine * 0.3, 0.95);
  },
  // 2 rocha
  (u, v) => {
    const r1 = ridge(u, v, 4, 5, 31), strata = Math.sin((v + fbm(u, v, 4, 4, 3, 33) * 0.35) * Math.PI * 2 * 9) * 0.5 + 0.5;
    const n = fbm(u, v, 16, 16, 4, 35), crack = sstep(0.93, 0.99, r1);
    const t = clamp01(n * 0.7 + strata * 0.25);
    let r = mix(0.33, 0.6, t), g = mix(0.31, 0.57, t), b = mix(0.29, 0.53, t);
    const lichen = sstep(0.68, 0.82, fbm(u, v, 8, 8, 3, 37)) * 0.35;
    r = mix(r, 0.44, lichen); g = mix(g, 0.46, lichen); b = mix(b, 0.3, lichen);
    const d = 1 - crack * 0.6;
    set(r * d, g * d, b * d, clamp01(r1 * 0.55 + n * 0.35 + strata * 0.1 - crack * 0.3), 0.82);
  },
  // 3 terra / estrada
  (u, v) => {
    const n = fbm(u, v, 8, 8, 4, 41), w = worley(u, v, 24, 43);
    const pebble = sstep(0.2, 0.1, w.f1) * (w.id > 0.35 ? 1 : 0);
    const t = clamp01(n * 0.8 + 0.1);
    let r = mix(0.24, 0.42, t), g = mix(0.17, 0.3, t), b = mix(0.1, 0.19, t);
    r = mix(r, 0.42 + w.id * 0.12, pebble * 0.6); g = mix(g, 0.37 + w.id * 0.1, pebble * 0.6); b = mix(b, 0.31 + w.id * 0.08, pebble * 0.6);
    set(r, g, b, clamp01(n * 0.5 + pebble * 0.5), 0.93);
  },
  // 4 areia
  (u, v) => {
    const n = fbm(u, v, 8, 8, 4, 51), fine = vn(u * 256, v * 256, 256, 256, 52);
    const rip = Math.sin((u + fbm(u, v, 4, 4, 3, 53) * 0.25) * Math.PI * 2 * 16) * 0.5 + 0.5;
    const t = clamp01(n * 0.6 + fine * 0.3 + rip * 0.1);
    set(mix(0.63, 0.82, t), mix(0.55, 0.73, t), mix(0.4, 0.55, t), rip * 0.5 + fine * 0.3 + n * 0.2, 0.97);
  },
  // 5 neve
  (u, v) => {
    const n = fbm(u, v, 6, 6, 5, 61), fine = vn(u * 256, v * 256, 256, 256, 62);
    const t = clamp01(n * 0.8 + fine * 0.2);
    set(mix(0.8, 0.97, t), mix(0.84, 0.98, t), mix(0.9, 1.0, t), n * 0.8 + fine * 0.2, 0.55);
  },
  // 6 cinza dos Ermos
  (u, v) => {
    const n = fbm(u, v, 8, 8, 4, 71), w = worley(u, v, 10, 73);
    const crack = sstep(0.06, 0.0, w.f2 - w.f1);
    const t = clamp01(n * 0.8 + w.id * 0.2);
    let r = mix(0.3, 0.5, t), g = mix(0.27, 0.45, t), b = mix(0.28, 0.47, t);
    r *= 1 - crack * 0.55; g *= 1 - crack * 0.55; b *= 1 - crack * 0.5;
    set(r, g, b, clamp01(n * 0.6 + 0.4 - crack * 0.6), 0.96);
  },
  // 7 calçamento de pedras (praças)
  (u, v) => {
    const w = worley(u, v, 14, 81), n = fbm(u, v, 16, 16, 3, 83);
    const gap = sstep(0.14, 0.03, w.f2 - w.f1);
    const dome = clamp01(1 - w.f1 * 1.2);
    const tone = 0.3 + w.id * 0.17 + n * 0.08;
    let r = tone * 0.98, g = tone * 0.92, b = tone * 0.84;
    r = mix(r, 0.17, gap); g = mix(g, 0.14, gap); b = mix(b, 0.1, gap);
    set(r, g, b, clamp01(dome * 0.7 + n * 0.3 - gap * 0.7), 0.85);
  },
];

// ---------- superfícies de arquitetura (albedo quase neutro; a cor da peça tinge) ----------
const SURF = {
  alvenaria: [(u, v) => {
    const rows = 7, row = Math.floor(v * rows), fy = v * rows - row;
    const off = hash(row, 0, 91) * 0.5, count = 3 + Math.floor(hash(row, 1, 91) * 2);
    const x = (u + off) * count, col = Math.floor(x), fx = x - col;
    const id = hash(wrap(col, count), row, 93);
    const edge = Math.min(fx, 1 - fx) * rows / count, edgeY = Math.min(fy, 1 - fy);   // em unidades de altura de fileira
    const e = Math.min(edge, edgeY);
    const n = fbm(u, v, 16, 16, 4, 95), chip = fbm(u, v, 32, 32, 2, 97);
    const mortar = sstep(0.08, 0.03, e + (chip - 0.5) * 0.05);
    const tone = 0.68 + id * 0.24 + (n - 0.5) * 0.16;
    set(mix(tone, 0.6, mortar), mix(tone * 0.97, 0.58, mortar), mix(tone * 0.92, 0.54, mortar), clamp01((1 - mortar) * (0.65 + n * 0.35) * sstep(0, 0.14, e + 0.04)), 0.88);
  }, 2.4, 4],
  reboco: [(u, v) => {
    const n = fbm(u, v, 4, 4, 5, 101), fine = vn(u * 128, v * 128, 128, 128, 103), stain = sstep(0.6, 0.85, fbm(u, v, 3, 3, 4, 105));
    const w = worley(u, v, 5, 107), crack = sstep(0.018, 0.0, w.f2 - w.f1) * sstep(0.55, 0.75, fbm(u, v, 6, 6, 2, 109));
    const t = 0.86 + (n - 0.5) * 0.12 + (fine - 0.5) * 0.05 - stain * 0.12;
    set(t * (1 - crack * 0.4), t * 0.98 * (1 - crack * 0.4), t * 0.94 * (1 - crack * 0.4), clamp01(n * 0.4 + fine * 0.5 - crack * 0.5), 0.93);
  }, 3, 1.5],
  madeira: [(u, v) => {
    const planks = 5, x = u * planks, col = Math.floor(x), fx = x - col;
    const grain = fbm(u, v, 160, 6, 4, 111 + col * 7), rings = Math.sin((u * 40 + grain * 6 + hash(col, 0, 113) * 20) * Math.PI) * 0.5 + 0.5;
    const gap = sstep(0.04, 0.0, Math.min(fx, 1 - fx));
    const w = worley(u * planks % 1, v, 3, 115 + col), knot = sstep(0.12, 0.02, w.f1) * (w.id > 0.6 ? 1 : 0);
    const tone = 0.7 + hash(col, 1, 117) * 0.15 + (grain - 0.5) * 0.25 + rings * 0.06 - knot * 0.3;
    set(tone * (1 - gap * 0.7), tone * 0.83 * (1 - gap * 0.7), tone * 0.66 * (1 - gap * 0.7), clamp01(0.6 + (grain - 0.5) * 0.5 - gap * 0.7 - knot * 0.2), 0.8);
  }, 1.6, 2.5],
  telhas: [(u, v) => {
    const rows = 8, row = Math.floor(v * rows), fy = v * rows - row;
    const cols = 6, off = (row % 2) * 0.5 / cols, x = (u + off) * cols, col = Math.floor(x), fx = x - col;
    const arc = Math.sqrt(Math.max(0, 1 - (fx * 2 - 1) ** 2));
    const lap = sstep(0.0, 0.25, fy);                   // cada fileira cobre a de baixo
    const id = hash(wrap(col, cols), row, 121), n = fbm(u, v, 16, 16, 3, 123);
    const gap = sstep(0.12, 0.0, arc);
    const tone = 0.82 + (id - 0.5) * 0.2 + (n - 0.5) * 0.1;
    set(tone * (1 - gap * 0.5) * (0.75 + lap * 0.25), tone * (1 - gap * 0.5) * (0.75 + lap * 0.25), tone * (1 - gap * 0.5) * (0.75 + lap * 0.25), clamp01(arc * 0.7 * lap + (1 - lap) * 0.2 + n * 0.1), 0.32 + n * 0.25);
  }, 1.5, 4],
  metal: [(u, v) => {
    const brush = fbm(u, v, 128, 4, 3, 131), pat = sstep(0.55, 0.8, fbm(u, v, 6, 6, 4, 133)), sc = sstep(0.985, 1, vn(u * 64, v * 256, 64, 256, 135));
    const t = 0.72 + (brush - 0.5) * 0.18 + sc * 0.2;
    set(mix(t, 0.45, pat), mix(t, 0.55, pat), mix(t, 0.48, pat), brush * 0.5 + pat * 0.2, 0.35 + pat * 0.45);
  }, 1, 1],
  tecido: [(u, v) => {
    const wv = Math.sin(u * Math.PI * 2 * 32) * Math.sin(v * Math.PI * 2 * 32), n = fbm(u, v, 8, 8, 3, 141);
    const t = 0.82 + wv * 0.06 + (n - 0.5) * 0.12;
    set(t, t, t, wv * 0.5 + 0.5, 1);
  }, 1, 0.8],
  casca: [(u, v) => {
    const fis = 1 - Math.abs(fbm(u, v, 12, 2, 3, 151) * 2 - 1), n = fbm(u, v, 16, 4, 4, 153), moss = sstep(0.6, 0.8, fbm(u, v, 4, 4, 3, 155)) * 0.35;
    const t = 0.45 + fis * 0.35 + (n - 0.5) * 0.15;
    set(mix(t, 0.35, moss), mix(t * 0.85, 0.42, moss), mix(t * 0.7, 0.22, moss), clamp01(fis * 0.8 + n * 0.2), 0.95);
  }, 1.2, 5],
};

// ---------- utilidades de textura ----------
function dataTex(data, N, srgb) {
  const t = new THREE.DataTexture(data, N, N, THREE.RGBAFormat);
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.generateMipmaps = true; t.minFilter = THREE.LinearMipmapLinearFilter; t.magFilter = THREE.LinearFilter;
  t.anisotropy = 8;
  if (srgb) t.colorSpace = THREE.SRGBColorSpace;
  t.needsUpdate = true;
  return t;
}
function arrayTex(layers, N, srgb) {
  const data = new Uint8Array(N * N * 4 * layers.length);
  layers.forEach((l, i) => data.set(l, i * N * N * 4));
  const t = new THREE.DataArrayTexture(data, N, N, layers.length);
  t.format = THREE.RGBAFormat;
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.generateMipmaps = true; t.minFilter = THREE.LinearMipmapLinearFilter; t.magFilter = THREE.LinearFilter;
  t.anisotropy = 8;
  if (srgb) t.colorSpace = THREE.SRGBColorSpace;
  t.needsUpdate = true;
  return t;
}
function canvasTex(draw, w = 512, h = 512) {
  const c = document.createElement('canvas'); c.width = w; c.height = h;
  draw(c.getContext('2d'), w, h);
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace; t.anisotropy = 8;
  return t;
}
const rnd = (() => { let s = 12345; return () => { s = (s * 16807) % 2147483647; return s / 2147483647; }; })();
const green = (l, sat = 1) => { const t = rnd(); return `rgb(${Math.round((40 + t * 50) * l)},${Math.round((78 + t * 60) * l * sat)},${Math.round((22 + t * 25) * l)})`; };

// cartão de folhas (copa das folhosas e arbustos)
function drawLeaves(g, w, h) {
  for (let i = 0; i < 420; i++) {
    const a = rnd() * Math.PI * 2, r = Math.sqrt(rnd()) * 0.46;
    const x = w / 2 + Math.cos(a) * r * w, y = h / 2 + Math.sin(a) * r * h;
    const s = (0.018 + rnd() * 0.02) * w;
    g.save(); g.translate(x, y); g.rotate(rnd() * Math.PI * 2);
    g.fillStyle = green(0.75 + (1 - r) * 0.5);
    g.beginPath(); g.ellipse(0, 0, s, s * 0.45, 0, 0, Math.PI * 2); g.fill();
    g.strokeStyle = 'rgba(20,40,10,0.35)'; g.lineWidth = 1; g.beginPath(); g.moveTo(-s, 0); g.lineTo(s, 0); g.stroke();
    g.restore();
  }
}
// cartão de galho de pinheiro (agulhas); o tronco fica à esquerda
function drawNeedles(g, w, h) {
  const twig = (x0, y0, x1, y1, len) => {
    g.strokeStyle = '#4a3624'; g.lineWidth = 3; g.beginPath(); g.moveTo(x0, y0); g.lineTo(x1, y1); g.stroke();
    const n = Math.floor(len / 2.2);
    for (let i = 0; i < n; i++) {
      const t = i / n, x = x0 + (x1 - x0) * t, y = y0 + (y1 - y0) * t;
      const l = (1 - t * 0.6) * (0.08 + rnd() * 0.03) * h;
      for (const sgn of [-1, 1]) {
        const ang = Math.atan2(y1 - y0, x1 - x0) + sgn * (0.9 + rnd() * 0.4);
        g.strokeStyle = green(0.7 + rnd() * 0.4, 1.05); g.lineWidth = 1.6;
        g.beginPath(); g.moveTo(x, y); g.lineTo(x + Math.cos(ang) * l, y + Math.sin(ang) * l); g.stroke();
      }
    }
  };
  twig(w * 0.02, h * 0.5, w * 0.97, h * 0.47, w * 0.95);
  for (let i = 0; i < 7; i++) {
    const x = w * (0.15 + i * 0.11), s = i % 2 ? 1 : -1;
    twig(x, h * 0.5, x + w * 0.16, h * (0.5 + s * 0.26), w * 0.3);
  }
}
// cartão de grama (tufo)
function drawGrass(g, w, h, herb) {
  for (let i = 0; i < (herb ? 26 : 44); i++) {
    const x = w * (0.08 + rnd() * 0.84), bw = w * (herb ? 0.03 : 0.012) * (0.7 + rnd());
    const top = h * (0.05 + rnd() * 0.4), bend = (rnd() - 0.5) * w * 0.25;
    const t = rnd();
    g.fillStyle = `rgb(${Math.round(60 + t * 70)},${Math.round(100 + t * 60)},${Math.round(30 + t * 25)})`;
    g.beginPath(); g.moveTo(x - bw, h); g.quadraticCurveTo(x + bend * 0.5, (h + top) / 2, x + bend, top); g.quadraticCurveTo(x + bend * 0.5 + bw * 0.3, (h + top) / 2, x + bw, h); g.fill();
  }
  if (herb) for (let i = 0; i < 9; i++) { g.fillStyle = '#f3ee9a'; g.beginPath(); g.arc(w * (0.2 + rnd() * 0.6), h * (0.1 + rnd() * 0.35), w * 0.018, 0, Math.PI * 2); g.fill(); }
}

// ---------- geração assíncrona (cede o controle para a tela mostrar o progresso) ----------
export const TEX = {};
const tick = () => new Promise((r) => setTimeout(r, 0));
export async function generateTextures(progress) {
  const N = 512, layersA = [], layersN = [];
  for (let i = 0; i < TERRAIN.length; i++) {
    progress?.(`texturas do terreno ${i + 1}/${TERRAIN.length}`);
    const s = build(N, TERRAIN[i], i === 2 ? 5 : 2.5);
    layersA.push(s.alb); layersN.push(s.nrm);
    if (i === 2) { TEX.rockAlb = dataTex(s.alb, N, true); TEX.rockNrm = dataTex(s.nrm, N, false); }
    await tick();
  }
  TEX.terrainAlb = arrayTex(layersA, N, true);
  TEX.terrainNrm = arrayTex(layersN, N, false);
  TEX.surf = {};
  for (const [name, [fn, scale, strength]] of Object.entries(SURF)) {
    progress?.(`materiais: ${name}`);
    const s = build(256, fn, strength);
    TEX.surf[name] = { alb: dataTex(s.alb, 256, true), nrm: dataTex(s.nrm, 256, false), scale: 1 / scale };
    await tick();
  }
  TEX.surf.rocha = { alb: TEX.rockAlb, nrm: TEX.rockNrm, scale: 1 / 4 };
  progress?.('água e céu');
  const water = build(256, (u, v) => { const n = fbm(u, v, 4, 4, 5, 161); set(0, 0, 0, n, 0.1); }, 3);
  TEX.waterNrm = dataTex(water.nrm, 256, false);
  const macro = build(256, (u, v) => { const n = fbm(u, v, 4, 4, 5, 171); set(n, n, n, n); }, 1);
  TEX.macro = dataTex(macro.alb, 256, false);
  await tick();
  progress?.('folhagem');
  TEX.leaves = canvasTex(drawLeaves);
  TEX.needles = canvasTex(drawNeedles, 512, 256);
  TEX.grass = canvasTex((g, w, h) => drawGrass(g, w, h, false), 256, 256);
  TEX.herb = canvasTex((g, w, h) => drawGrass(g, w, h, true), 256, 256);
  await tick();
  await applyNatureTextures(TEX, progress);
  return TEX;
}
