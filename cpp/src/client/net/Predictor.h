#pragma once
// Predição do próprio personagem (fase 8): o andar, o correr, o agachar e o pulo no estado livre,
// com as mesmas funções do CharacterMotor que o servidor usa (core/movement). Cada comando enviado
// vale um passo do servidor; quando um snapshot confirma o comando `lastCommandSeq`, a predição volta
// ao estado do servidor e refaz os comandos ainda não confirmados. A diferença entre o que estava na
// tela e a nova previsão some aos poucos (sem saltos), e um teleporte (mais de 4 m) entra direto.
//
// Fora do estado livre (golpes, técnicas, esquiva, canalização, derrubado), com clique para andar ou
// observando o céu, a predição desliga e vale a posição do servidor.
#include <cstdint>
#include <deque>
#include <optional>

#include <glm/glm.hpp>

#include "core/Math.h"
#include "core/data/GameData.h"
#include "core/movement/CharacterMotor.h"
#include "core/protocol/PlayerCommand.h"
#include "core/protocol/Snapshot.h"
#include "core/world/StaticWorld.h"

namespace rpg::client {

class Predictor {
 public:
  explicit Predictor(const GameData& data) : data_(data) {}

  // Um comando enviado (um por passo do servidor, `dt` = 1/tickRate).
  void sent(const protocol::PlayerCommand& cmd, double dt);
  // Snapshot mais novo do próprio personagem.
  void reconcile(const protocol::EntityState& me, const protocol::PrivateState& self, const StaticMap& map);
  // A correção visual decai a cada quadro.
  void update(double frameDt);
  void reset();

  bool active() const { return active_; }
  // Posição para desenhar e para a câmera: entre os dois últimos passos (`alpha` = fração do passo
  // atual já decorrida), mais a correção suavizada.
  glm::dvec3 position(double alpha = 1.0) const;
  std::size_t pending() const { return unacked_.size(); }
  double lastError() const { return lastError_; }

  // Um passo do estado livre (o trecho de locomoção de updatePlayer). Público para os testes.
  struct State {
    XZ pos;
    double y = 0;
    std::optional<motor::Air> air;
    double landT = 0, vigor = 0, metamorphT = 0, yaw = 0;
  };
  struct Inputs {
    motor::LoadState load = motor::LoadState::Normal;
    double armorSpeed = 1.0;
    bool mounted = false, cinematic = false;
  };
  static void step(const GameData& data, const StaticMap& map, State& s, const Inputs& in, const protocol::PlayerCommand& cmd,
                   const protocol::PlayerCommand& prev, double dt);

 private:
  struct Sent {
    protocol::PlayerCommand cmd;
    double dt = 0;
  };
  const GameData& data_;
  std::deque<Sent> unacked_;
  protocol::PlayerCommand ackedCmd_;  // o último comando confirmado (toques novos = contador diferente)
  State state_;
  glm::dvec3 prevPos_{0.0};
  Inputs inputs_;
  const StaticMap* map_ = nullptr;
  protocol::PlayerCommand lastSent_;
  glm::dvec3 offset_{0.0};
  bool active_ = false, have_ = false;
  double lastError_ = 0;
};

}  // namespace rpg::client
