#pragma once
// Buffer de snapshots e interpolação com atraso: o cliente desenha o mundo um pouco no passado
// (`delayTicks` passos), entre os dois snapshots que cercam esse instante. Assim o movimento fica
// liso mesmo com snapshots perdidos, atrasados ou fora de ordem (canal não confiável).
//
// O relógio do servidor é o número do passo: tempo = tick / tickRate. O deslocamento entre o
// relógio local e o do servidor é estimado a cada snapshot e suavizado.
#include <cstdint>
#include <deque>
#include <optional>

#include "core/protocol/Snapshot.h"

namespace rpg::client {

class SnapshotInterpolator {
 public:
  explicit SnapshotInterpolator(double tickRate = 30.0, double delayTicks = 1.5)
      : tickRate_(tickRate), delayTicks_(delayTicks) {}

  void setTickRate(double r) { tickRate_ = r; }
  void setDelayTicks(double d) { delayTicks_ = d; }
  double delayTicks() const { return delayTicks_; }

  // Guarda um snapshot recebido em `localTime` (s). Snapshots mais velhos que o mais novo já visto
  // são descartados (chegaram fora de ordem).
  void push(protocol::Snapshot snap, double localTime);

  // O mais recente (estado privado e do mundo: o HUD mostra o mais novo, sem atraso).
  const protocol::Snapshot* latest() const { return buffer_.empty() ? nullptr : &buffer_.back(); }

  // Entidades interpoladas para desenhar em `localTime`.
  std::vector<protocol::EntityState> sample(double localTime);

  // Instante do servidor (s) que está sendo desenhado.
  double renderServerTime(double localTime) const;
  std::size_t buffered() const { return buffer_.size(); }
  std::uint64_t dropped() const { return dropped_; }

  void clear();

 private:
  double tickRate_;
  double delayTicks_;
  std::deque<protocol::Snapshot> buffer_;
  std::optional<double> offset_;  // servidor − local
  std::uint64_t dropped_ = 0;
};

// Interpola uma entidade entre dois estados (posição e ângulos contínuos; o resto vem de `b`).
protocol::EntityState lerpEntity(const protocol::EntityState& a, const protocol::EntityState& b, double t);

}  // namespace rpg::client
