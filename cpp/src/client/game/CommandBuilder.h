#pragma once
// Input do quadro → PlayerCommand (o que player.js lia do teclado dentro de `updatePlayer`).
// Os toques viram contadores que só crescem: um comando perdido não perde o toque.
#include <cstdint>

#include "client/Input.h"
#include "client/game/Aim.h"
#include "core/protocol/PlayerCommand.h"

namespace rpg::client {

struct CommandContext {
  double camYaw = 0;
  bool uiOpen = false;   // painel/menu aberto: nada de mover nem agir
  bool typing = false;   // campo de texto com foco: teclas não são do jogo
  AimResult aim;
};

class CommandBuilder {
 public:
  // Monta o comando do quadro. `tabChord`: Tab segurado com 1/2/3 escolhe a câmera, não a poção.
  protocol::PlayerCommand build(const InputState& in, const CommandContext& ctx, bool tabChord);

  // O comando mais recente (os passos do servidor reaproveitam o último enquanto não chega outro).
  const protocol::PlayerCommand& last() const { return cmd_; }

 private:
  protocol::PlayerCommand cmd_;
  std::uint32_t seq_ = 0;
};

}  // namespace rpg::client
