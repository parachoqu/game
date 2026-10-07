#pragma once
// Snapshot em delta (fase 8): o servidor guarda os últimos snapshots que mandou a cada sessão e, contra
// o mais novo que o cliente confirmou (PlayerCommand::ackSnapshot), manda só o que mudou: as entidades
// novas ou diferentes, as que saíram, e o estado do mundo e o privado quando mudaram. Sem base
// confirmada (entrada no jogo, perdas demais), vai o snapshot inteiro (`base` = 0).
//
// O canal de snapshots não é confiável: um delta cuja base o cliente não tem é descartado, e o próximo
// (contra a base que o cliente confirmou de fato) o substitui.
#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

#include "core/protocol/Snapshot.h"

namespace rpg::protocol {

struct SnapshotDelta {
  std::uint32_t seq = 0;
  std::uint32_t base = 0;     // 0 = completo
  std::uint8_t worldMode = 1; // 0 = igual à base, 1 = presente
  WorldState world;
  std::uint8_t selfMode = 2;  // 0 = igual à base, 1 = presente, 2 = ausente (sem personagem)
  PrivateState self;
  std::vector<EntityState> changed;
  std::vector<std::uint32_t> removed;
  template <class A>
  void io(A& a) {
    a(seq); a(base); a(worldMode);
    if (worldMode == 1) a(world);
    a(selfMode);
    if (selfMode == 1) a(self);
    a(changed); a(removed);
  }
};

// Servidor: um por sessão.
class DeltaEncoder {
 public:
  static constexpr std::size_t kHistory = 32;
  // Monta o próximo delta contra `acked` (se ainda estiver no histórico) e guarda o snapshot.
  SnapshotDelta encode(const Snapshot& snap, std::uint32_t acked);
  void reset() { history_.clear(); }

 private:
  struct Sent {
    std::uint32_t seq = 0;
    Bytes world, self;
    bool hasSelf = false;
    std::unordered_map<std::uint32_t, Bytes> entities;
  };
  std::deque<Sent> history_;
  std::uint32_t seq_ = 0;
};

// Cliente: refaz o snapshot inteiro a partir da base.
class DeltaDecoder {
 public:
  static constexpr std::size_t kHistory = 32;
  // Nada se a base não estiver aqui (ou se o delta for mais velho que o último refeito).
  std::optional<Snapshot> decode(const SnapshotDelta& d);
  std::uint32_t lastSeq() const { return last_; }
  void reset() {
    history_.clear();
    last_ = 0;
  }

 private:
  std::deque<std::pair<std::uint32_t, Snapshot>> history_;
  std::uint32_t last_ = 0;
};

}  // namespace rpg::protocol
