#pragma once
// Geradores pseudoaleatórios determinísticos.
//
// A demo usava Math.random() nas regras (perda no Full Loot, bônus de coleta, próxima estrela…), o que
// impede repetir uma partida. Aqui toda a aleatoriedade da simulação passa por um gerador com semente:
// mesma semente + mesmos comandos = mesmo estado, o que permite testes, replays e servidor autoritativo.
//
//   Mulberry32 — cópia bit a bit do `rng`/`mulberry` do JS (world.js, noise.js, sky.js). Usado onde a
//                demo já tinha semente fixa, para reproduzir exatamente o mesmo mundo.
//   Pcg32      — gerador geral da simulação (substitui Math.random).
#include <cstddef>
#include <cstdint>
#include <span>

#include "core/Types.h"

namespace rpg {

class Mulberry32 {
 public:
  // O JS faz `seed |= 0`: qualquer inteiro de 32 bits (inclusive negativo) vale como semente.
  explicit Mulberry32(std::int32_t seed) : state_(static_cast<std::uint32_t>(seed)) {}

  // Próximo valor em [0, 1), idêntico ao da função do JS.
  double next();

 private:
  std::uint32_t state_;
};

// PCG32 (O'Neill, pcg-random.org): XSH-RR 64/32.
class Pcg32 {
 public:
  explicit Pcg32(std::uint64_t seed, std::uint64_t stream = 0x5851f42d4c957f2dULL);

  std::uint32_t nextU32();

  // [0, 1) com 32 bits de mantissa.
  double next() { return nextU32() * (1.0 / 4294967296.0); }

  // state.js `rand(a, b)`: a + x · (b − a).
  Real range(Real a, Real b) { return a + next() * (b - a); }

  // state.js `randi(a, b)`: inteiro em [a, b], com os dois extremos incluídos.
  int rangeInt(int a, int b);

  // true com probabilidade p (o `Math.random() < p` do JS).
  bool chance(double p) { return next() < p; }

  // state.js `pick(arr)`: índice sorteado em [0, n). n precisa ser > 0.
  std::size_t index(std::size_t n);

  template <class T>
  const T& pick(std::span<const T> items) {
    return items[index(items.size())];
  }

  // Estado completo, para salvar e restaurar a simulação.
  struct State {
    std::uint64_t state;
    std::uint64_t inc;
    bool operator==(const State&) const = default;
  };
  State save() const { return {state_, inc_}; }
  void restore(const State& s) {
    state_ = s.state;
    inc_ = s.inc;
  }

 private:
  std::uint64_t state_ = 0;
  std::uint64_t inc_ = 0;
};

}  // namespace rpg
