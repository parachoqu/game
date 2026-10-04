#pragma once
// O mundo como o cliente o conhece neste quadro: entidades interpoladas (o que se desenha), o estado
// privado do próprio jogador e o estado compartilhado do mundo (os mais novos, sem atraso).
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "core/protocol/Snapshot.h"
#include "core/world/MapId.h"

namespace rpg::client {

class ClientSession;

class ClientWorld {
 public:
  // Lê o interpolador da sessão para `localTime`.
  void update(ClientSession& session, double localTime);
  // Para testes e ferramentas: define o quadro diretamente.
  void set(std::uint32_t playerId, std::vector<protocol::EntityState> entities, const protocol::Snapshot* latest);

  const std::vector<protocol::EntityState>& entities() const { return entities_; }
  const protocol::EntityState* find(std::uint32_t id) const;
  const protocol::EntityState* me() const { return find(playerId_); }
  const protocol::PrivateState* self() const { return latest_ && latest_->self ? &*latest_->self : nullptr; }
  const protocol::WorldState* world() const { return latest_ ? &latest_->world : nullptr; }
  std::uint32_t playerId() const { return playerId_; }
  bool ready() const { return me() != nullptr && self() != nullptr; }

  MapKind map() const;
  std::uint32_t instance() const;

  // Predição (jogo pela rede): o próprio personagem vai para a posição prevista.
  void placeSelf(double x, double y, double z);

 private:
  std::uint32_t playerId_ = 0;
  std::vector<protocol::EntityState> entities_;
  std::unordered_map<std::uint32_t, std::size_t> index_;
  std::optional<protocol::Snapshot> latest_;
};

}  // namespace rpg::client
