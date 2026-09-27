#pragma once
// Funções numéricas de state.js e player.js, com o mesmo comportamento (inclusive nas bordas).
#include <cmath>
#include <initializer_list>
#include <limits>
#include <numbers>

#include "core/JsMath.h"
#include "core/Types.h"

namespace rpg {

inline constexpr Real kPi = std::numbers::pi;
inline constexpr Real kTau = 2.0 * std::numbers::pi;

// state.js: clamp = (v < a ? a : v > b ? b : v)
constexpr Real clamp(Real v, Real a, Real b) { return v < a ? a : v > b ? b : v; }

constexpr Real lerp(Real a, Real b, Real t) { return a + (b - a) * t; }

// state.js `smooth`: smoothstep de e0 a e1.
constexpr Real smooth(Real e0, Real e1, Real x) {
  const Real t = clamp((x - e0) / (e1 - e0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

// state.js `dist2` (apesar do nome, é a distância, não o quadrado).
inline Real distXZ(Real ax, Real az, Real bx, Real bz) {
  const Real dx = ax - bx, dz = az - bz;
  return std::sqrt(dx * dx + dz * dz);
}

// state.js `angleDiff`: a - b levado a (-π, π] pelos mesmos laços do JS.
inline Real angleDiff(Real a, Real b) {
  Real d = a - b;
  while (d > kPi) d -= kTau;
  while (d < -kPi) d += kTau;
  return d;
}

// player.js `lerpAngle`: anda de a para b pelo caminho mais curto; t é limitado a 1.
inline Real lerpAngle(Real a, Real b, Real t) { return a + angleDiff(b, a) * (t < 1.0 ? t : 1.0); }

// player.js `k(rate, dt)`: suavização por tempo, igual a 30, 60 ou 144 quadros por segundo.
inline Real expSmooth(Real rate, Real dt) { return 1.0 - js::exp(-rate * dt); }

// ---------------------------------------------------------------- Math.* do JS, bit a bit
// A simulação reproduz a demo exatamente (testes de paridade), então onde o JavaScript e a
// biblioteca C++ divergem no último bit, vale o comportamento do JS.

// Math.round: empate vai para +∞ (Math.round(-2.5) = -2); std::round afastaria do zero (-3).
inline Real jsRound(Real x) {
  const Real r = std::floor(x);
  return x - r >= 0.5 ? r + 1.0 : r;
}

// Math.hypot(a, b) do V8: divide pelo maior módulo e soma os quadrados com compensação de Kahan.
// std::hypot arredonda de outro jeito; a fórmula ingênua √(a² + b²) difere do V8 em ~35% dos casos.
inline Real jsHypot(Real a, Real b) {
  a = std::fabs(a);
  b = std::fabs(b);
  if (std::isinf(a) || std::isinf(b)) return std::numeric_limits<Real>::infinity();
  if (std::isnan(a) || std::isnan(b)) return std::numeric_limits<Real>::quiet_NaN();
  const Real max = a > b ? a : b;
  if (max == 0.0) return 0.0;
  Real sum = 0.0, compensation = 0.0;
  for (const Real v : {a, b}) {
    const Real n = v / max;
    const Real summand = n * n - compensation;
    const Real preliminary = sum + summand;
    compensation = (preliminary - sum) - summand;
    sum = preliminary;
  }
  return std::sqrt(sum) * max;
}

// Math.hypot(a, b, c) do V8: o mesmo laço de Kahan com três termos.
inline Real jsHypot3(Real a, Real b, Real c) {
  a = std::fabs(a);
  b = std::fabs(b);
  c = std::fabs(c);
  if (std::isinf(a) || std::isinf(b) || std::isinf(c)) return std::numeric_limits<Real>::infinity();
  if (std::isnan(a) || std::isnan(b) || std::isnan(c)) return std::numeric_limits<Real>::quiet_NaN();
  Real max = a > b ? a : b;
  max = max > c ? max : c;
  if (max == 0.0) return 0.0;
  Real sum = 0.0, compensation = 0.0;
  for (const Real v : {a, b, c}) {
    const Real n = v / max;
    const Real summand = n * n - compensation;
    const Real preliminary = sum + summand;
    compensation = (preliminary - sum) - summand;
    sum = preliminary;
  }
  return std::sqrt(sum) * max;
}

// Math.sign(v || 1): o sinal, tratando zero (e NaN) como positivo.
inline Real signOr1(Real v) { return v < 0.0 ? -1.0 : 1.0; }

}  // namespace rpg
