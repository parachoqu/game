#pragma once
// Funções numéricas de state.js e player.js, com o mesmo comportamento (inclusive nas bordas).
#include <cmath>
#include <numbers>

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
inline Real expSmooth(Real rate, Real dt) { return 1.0 - std::exp(-rate * dt); }

// Math.hypot de dois termos, com o nome usado no JS para ler o porte.
inline Real hypot2(Real x, Real z) { return std::hypot(x, z); }

}  // namespace rpg
