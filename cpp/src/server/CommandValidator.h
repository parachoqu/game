#pragma once
// O que um cliente pode mandar (a base anti-trapaça da fase 8). O servidor já confere distância,
// moedas, capacidade e combate em cada pedido; aqui ficam os limites do próprio fluxo:
//
//   - taxa: comandos e pedidos por segundo (balde de fichas), o resto é descartado e contado;
//   - números: movimento em −1…1, ângulos e pontos de mira finitos, mira a no máximo 300 m do corpo,
//     só os bits conhecidos de teclas seguradas.
#include <cstdint>

#include "core/Math.h"
#include "core/protocol/PlayerCommand.h"

namespace rpg::server {

struct ValidatorLimits {
  double commandsPerSecond = 90;  // o cliente manda um por passo (30/s); folga para rajadas da rede
  double commandBurst = 45;
  double requestsPerSecond = 30;
  double requestBurst = 40;
  double aimRange = 300;  // m entre o corpo e o ponto mirado
};

class CommandValidator {
 public:
  explicit CommandValidator(ValidatorLimits limits = {}) : limits_(limits), cmdTokens_(limits.commandBurst), reqTokens_(limits.requestBurst) {}

  // Normaliza `cmd` no lugar. false: descartar (taxa estourada). `now` em segundos (relógio do servidor).
  bool command(protocol::PlayerCommand& cmd, double px, double py, double pz, double now);
  // false: pedido acima da taxa.
  bool request(double now);

  std::uint64_t dropped() const { return dropped_; }
  std::uint64_t fixed() const { return fixed_; }

 private:
  static bool refill(double& tokens, double& last, double rate, double burst, double now);

  ValidatorLimits limits_;
  double cmdTokens_, reqTokens_;
  double cmdLast_ = -1, reqLast_ = -1;
  std::uint64_t dropped_ = 0, fixed_ = 0;
};

}  // namespace rpg::server
