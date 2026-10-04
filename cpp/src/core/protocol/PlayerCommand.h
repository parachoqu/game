#pragma once
// Intenção do jogador num passo (canal Commands, não confiável). Substitui a leitura direta do teclado
// e do mouse que a demo fazia dentro de `updatePlayer` (`down('KeyW')`, `hit('Space')`, `input.mouse`).
//
// O cliente manda o que foi pedido, não o resultado: a simulação calcula a direção do passo a partir
// das teclas e do giro da câmera com a mesma conta da demo, e decide se a ação é possível.
//
// Toques (pular, esquivar, técnicas…) vão como contadores: cada toque incrementa o seu. Um pacote
// perdido não perde o toque, porque o seguinte chega com o contador já adiantado.
#include <array>
#include <cstdint>

#include "core/protocol/ByteStream.h"

namespace rpg::protocol {

enum Held : std::uint16_t {
  kHeldRun = 1 << 0,     // Shift
  kHeldCrouch = 1 << 1,  // Ctrl
  kHeldAim = 1 << 2,     // botão direito
  kHeldUiOpen = 1 << 3,  // painel aberto: a simulação não aceita ações
};

enum class Press : std::uint8_t {
  Jump,      // Espaço (com Shift: salto impulsionado)
  Dodge,     // C
  SkillQ,    // Q
  SkillE,    // E
  Potion,    // 1 (sem Tab)
  Fire,      // clique esquerdo mirando
  Interact,  // F
  Mount,     // R
  Count
};

struct PlayerCommand {
  std::uint32_t seq = 0;         // número do comando (reconciliação da predição)
  std::uint32_t clientTick = 0;
  std::uint32_t ackSnapshot = 0; // último snapshot que o cliente refez (base do próximo delta)
  std::int8_t moveForward = 0;   // W − S
  std::int8_t moveRight = 0;     // D − A
  double camYaw = 0;             // giro da câmera: WASD é relativo a ele
  std::uint16_t held = 0;
  std::array<std::uint8_t, static_cast<std::size_t>(Press::Count)> presses{};  // contadores

  // Mira calculada pelo cliente (raio da câmera contra entidades e terreno; player.js `updateAim`).
  double aimYaw = 0, aimPitch = 0, aimDist = 0;
  double aimX = 0, aimY = 0, aimZ = 0;
  std::uint32_t aimEnemy = 0;    // EntityId sob a mira (0 = nenhum)

  bool has(Held h) const { return (held & h) != 0; }
  std::uint8_t count(Press p) const { return presses[static_cast<std::size_t>(p)]; }
  void press(Press p) { ++presses[static_cast<std::size_t>(p)]; }

  template <class A>
  void io(A& a) {
    a(seq);
    a(clientTick);
    a(ackSnapshot);
    a(moveForward);
    a(moveRight);
    a.f32(camYaw);
    a(held);
    a(presses);
    a.f32(aimYaw);
    a.f32(aimPitch);
    a.f32(aimDist);
    a.f32(aimX);
    a.f32(aimY);
    a.f32(aimZ);
    a(aimEnemy);
  }
};

}  // namespace rpg::protocol
