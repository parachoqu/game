#pragma once
// O mundo autoritativo: a interface da simulação para quem está fora dela (servidor, testes).
// Substitui o estado global `G` e o laço `tick()` de main.js.
//
//   addPlayer / removePlayer   — personagens em jogo (um por sessão)
//   setCommand                 — intenção do passo (PlayerCommand)
//   handleRequest              — ação discreta (compra, fabricação, equipar, portal…)
//   step                       — um passo fixo, na ordem do tick() da demo
//   snapshot / takeEvents      — o que o servidor manda a cada cliente
//
// Não conhece câmera, tela, teclado, som nem texto de interface.
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "core/GameClock.h"
#include "core/Random.h"
#include "core/Types.h"
#include "core/data/GameData.h"
#include "core/protocol/PlayerCommand.h"
#include "core/protocol/Requests.h"
#include "core/protocol/Snapshot.h"
#include "core/world/StaticWorld.h"
#include "sim/Sim.h"

namespace rpg::sim {

struct WorldConfig {
  std::uint64_t seed = 1;
  SimOptions options;
  bool devCommands = false;  // aceita ReqDev (window.__demo da demo)
};

class World {
 public:
  // `data` e `statics` são carregados uma vez e precisam viver mais que o World.
  World(const GameData& data, const StaticWorld& statics, WorldConfig config = {});
  // Com gerador injetado (testes de paridade: Mulberry32 com a semente do harness da demo).
  World(const GameData& data, const StaticWorld& statics, std::unique_ptr<IRandom> rng, SimOptions options,
        bool devCommands = false);
  ~World();
  World(const World&) = delete;
  World& operator=(const World&) = delete;

  // ---------------------------------------------------------------- jogadores
  // `newCharacter`: registra a chegada no Livro (personagem novo; um save carregado não registra).
  EntityId addPlayer(const PlayerProfile& profile, std::uint32_t session = 0, bool newCharacter = true);
  void removePlayer(EntityId id);
  void setCommand(EntityId player, const protocol::PlayerCommand& cmd);
  void handleRequest(EntityId player, const protocol::Request& req);

  // ---------------------------------------------------------------- passo
  void step(Seconds dt);

  // ---------------------------------------------------------------- saída
  // Snapshot visto por `player` (entidades num raio, estado privado dele, estado do mundo).
  protocol::Snapshot snapshot(EntityId player, double radius = 160) const;
  // Eventos gerados desde a última chamada, com destinatário.
  std::vector<OutEvent> takeEvents();
  // O Livro inteiro de um jogador (entrada no jogo, save carregado).
  protocol::EvBookSync bookSync(EntityId player) const;

  // ---------------------------------------------------------------- consulta
  const GameData& data() const { return s_->data; }
  const StaticWorld& statics() const { return s_->statics; }
  Tick tick() const { return s_->tick; }
  Seconds time() const { return s_->time; }
  ClockTime clock() const { return clockAt(s_->time, s_->data.balance.clock); }
  bool paused() const { return paused_; }
  void setTime(Seconds t) { s_->time = t; }

  // Estado interno: testes, persistência e ferramentas de desenvolvimento.
  Sim& state() { return *s_; }
  const Sim& state() const { return *s_; }

 private:
  struct Input {
    protocol::PlayerCommand cmd;
    std::array<std::uint8_t, static_cast<std::size_t>(protocol::Press::Count)> seen{};
    bool first = true;
  };
  void populate();
  void handleDev(Player& p, const protocol::ReqDev& r);
  Input& inputOf(EntityId id);
  const Input* findInput(EntityId id) const;

  std::unique_ptr<Sim> s_;
  bool devCommands_ = false;
  bool paused_ = false;
  std::vector<std::pair<EntityId, Input>> inputs_;
};

}  // namespace rpg::sim
