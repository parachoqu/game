#include "core/Rng.h"

#include <cmath>

#include "core/Assert.h"

namespace rpg {

// JS:
//   seed |= 0; seed = seed + 0x6D2B79F5 | 0;
//   let t = Math.imul(seed ^ seed >>> 15, 1 | seed);
//   t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
//   return ((t ^ t >>> 14) >>> 0) / 4294967296;
// Em aritmética de 32 bits sem sinal os bits são os mesmos: Math.imul e `| 0` são módulo 2^32, e a
// soma `t + imul(...)` passa por ToInt32 no `^`, que também é módulo 2^32.
double Mulberry32::next() {
  state_ += 0x6D2B79F5u;
  std::uint32_t t = (state_ ^ (state_ >> 15)) * (1u | state_);
  t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
  return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
}

Pcg32::Pcg32(std::uint64_t seed, std::uint64_t stream) {
  inc_ = (stream << 1u) | 1u;
  nextU32();
  state_ += seed;
  nextU32();
}

std::uint32_t Pcg32::nextU32() {
  const std::uint64_t old = state_;
  state_ = old * 6364136223846793005ULL + inc_;
  const auto xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
  const auto rot = static_cast<std::uint32_t>(old >> 59u);
  return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

int Pcg32::rangeInt(int a, int b) {
  RPG_ASSERT(b >= a, "rangeInt: intervalo vazio");
  return static_cast<int>(std::floor(range(a, static_cast<Real>(b) + 1.0)));
}

std::size_t Pcg32::index(std::size_t n) {
  RPG_ASSERT(n > 0, "index: lista vazia");
  const auto i = static_cast<std::size_t>(next() * static_cast<double>(n));
  return i < n ? i : n - 1;
}

}  // namespace rpg
