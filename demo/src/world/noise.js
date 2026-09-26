// Ruído determinístico barato para o mundo: valor 1D e 2D, fbm e sorteio com semente.
// Sem alocação por consulta; o mesmo par (x, z) devolve sempre o mesmo valor, em qualquer máquina.

export function rng(seed) {
  return () => {
    seed |= 0; seed = seed + 0x6D2B79F5 | 0;
    let t = Math.imul(seed ^ seed >>> 15, 1 | seed);
    t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
    return ((t ^ t >>> 14) >>> 0) / 4294967296;
  };
}

// hash inteiro → [0, 1)
export function hash2(x, z, seed = 0) {
  let h = Math.imul(x | 0, 374761393) ^ Math.imul(z | 0, 668265263) ^ Math.imul(seed | 0, 1274126177);
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
}

const fade = (t) => t * t * (3 - 2 * t);

// valor 2D em [0, 1]
export function vnoise(x, z, seed = 0) {
  const xi = Math.floor(x), zi = Math.floor(z);
  const u = fade(x - xi), v = fade(z - zi);
  const a = hash2(xi, zi, seed), b = hash2(xi + 1, zi, seed);
  const c = hash2(xi, zi + 1, seed), d = hash2(xi + 1, zi + 1, seed);
  return (a + (b - a) * u) * (1 - v) + (c + (d - c) * u) * v;
}

// fbm 2D em [0, 1], normalizado
export function fbm2(x, z, oct = 3, seed = 0) {
  let s = 0, amp = 1, f = 1, norm = 0;
  for (let i = 0; i < oct; i++) {
    s += vnoise(x * f + i * 17.31, z * f - i * 9.73, seed + i * 101) * amp;
    norm += amp; amp *= 0.5; f *= 2.03;
  }
  return s / norm;
}

// valor 1D em [0, 1]
export function vnoise1(x, seed = 0) {
  const xi = Math.floor(x), u = fade(x - xi);
  const a = hash2(xi, 7, seed), b = hash2(xi + 1, 7, seed);
  return a + (b - a) * u;
}

export const smoothstep = (e0, e1, x) => {
  const t = Math.min(1, Math.max(0, (x - e0) / (e1 - e0)));
  return t * t * (3 - 2 * t);
};
export const clamp01 = (v) => (v < 0 ? 0 : v > 1 ? 1 : v);
export const lerp = (a, b, t) => a + (b - a) * t;

// Suaviza um vetor por três passadas de caixa (≈ gaussiana de desvio `sigma`, em amostras).
export function blur1(arr, sigma) {
  const r = Math.max(1, Math.round(sigma * 0.9));
  let src = Float64Array.from(arr), dst = new Float64Array(arr.length);
  const n = arr.length;
  for (let pass = 0; pass < 3; pass++) {
    let acc = 0;
    for (let i = -r; i <= r; i++) acc += src[Math.min(n - 1, Math.max(0, i))];
    for (let i = 0; i < n; i++) {
      dst[i] = acc / (2 * r + 1);
      acc += src[Math.min(n - 1, i + r + 1)] - src[Math.max(0, i - r)];
    }
    [src, dst] = [dst, src];
  }
  for (let i = 0; i < n; i++) arr[i] = src[i];
  return arr;
}
