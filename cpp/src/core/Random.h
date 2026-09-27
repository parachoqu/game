#pragma once
// Fonte de aleatoriedade injetável da simulação.
//
// A simulação chama `next()` exatamente onde a demo chama `Math.random()`, na mesma ordem. O jogo usa
// `Pcg32Random` com a semente do mundo; os testes de paridade injetam `Mulberry32Random` com a mesma
// semente que o harness do bake instala no lugar do `Math.random` da demo, e o rastro tem que sair
// igual. A aleatoriedade só visual (partículas, tremor de câmera, olhares dos personagens) nunca passa
// por aqui: é do cliente.
#include <cmath>
#include <cstdint>
#include <memory>

#include "core/Rng.h"

namespace rpg {

class IRandom {
 public:
  virtual ~IRandom() = default;
  // Número em [0, 1).
  virtual double next() = 0;

  // state.js `rand(a, b)`: a + Math.random() * (b - a).
  double range(double a, double b) { return a + next() * (b - a); }
  // state.js `randi(a, b)`: Math.floor(rand(a, b + 1)).
  int rangeInt(int a, int b) { return static_cast<int>(std::floor(range(a, static_cast<double>(b) + 1.0))); }
  // state.js `pick(arr)`: índice Math.floor(Math.random() * n).
  std::size_t index(std::size_t n) { return static_cast<std::size_t>(std::floor(next() * static_cast<double>(n))); }
  bool chance(double p) { return next() < p; }
};

class Pcg32Random final : public IRandom {
 public:
  explicit Pcg32Random(std::uint64_t seed, std::uint64_t stream = 54) : rng_(seed, stream) {}
  double next() override { return rng_.next(); }
  Pcg32& engine() { return rng_; }

 private:
  Pcg32 rng_;
};

class Mulberry32Random final : public IRandom {
 public:
  explicit Mulberry32Random(std::int32_t seed) : rng_(seed) {}
  double next() override { return rng_.next(); }

 private:
  Mulberry32 rng_;
};

}  // namespace rpg
