#include "core/Noise.h"

#include <cmath>

namespace rpg {

std::int32_t jsToInt32(double v) {
  if (!std::isfinite(v)) return 0;
  const double t = std::trunc(v);
  const double m = std::fmod(t, 4294967296.0);  // sinal do dividendo, como no JS
  const double u = m < 0 ? m + 4294967296.0 : m;
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(u));
}

namespace {
// Math.imul: produto de 32 bits com sinal, o que em aritmética sem sinal é o mesmo padrão de bits.
std::uint32_t imul(std::uint32_t a, std::uint32_t b) { return a * b; }
std::uint32_t u32(double v) { return static_cast<std::uint32_t>(jsToInt32(v)); }
double fade(double t) { return t * t * (3 - 2 * t); }
}  // namespace

// noise.js:
//   let h = Math.imul(x | 0, 374761393) ^ Math.imul(z | 0, 668265263) ^ Math.imul(seed | 0, 1274126177);
//   h = Math.imul(h ^ (h >>> 13), 1274126177);
//   return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
double hash2(double x, double z, double seed) {
  std::uint32_t h = imul(u32(x), 374761393u) ^ imul(u32(z), 668265263u) ^ imul(u32(seed), 1274126177u);
  h = imul(h ^ (h >> 13), 1274126177u);
  return static_cast<double>(h ^ (h >> 16)) / 4294967296.0;
}

double vnoise(double x, double z, double seed) {
  const double xi = std::floor(x), zi = std::floor(z);
  const double u = fade(x - xi), v = fade(z - zi);
  const double a = hash2(xi, zi, seed), b = hash2(xi + 1, zi, seed);
  const double c = hash2(xi, zi + 1, seed), d = hash2(xi + 1, zi + 1, seed);
  return (a + (b - a) * u) * (1 - v) + (c + (d - c) * u) * v;
}

double fbm2(double x, double z, int octaves, double seed) {
  double s = 0, amp = 1, f = 1, norm = 0;
  for (int i = 0; i < octaves; i++) {
    s += vnoise(x * f + i * 17.31, z * f - i * 9.73, seed + i * 101) * amp;
    norm += amp;
    amp *= 0.5;
    f *= 2.03;
  }
  return s / norm;
}

}  // namespace rpg
