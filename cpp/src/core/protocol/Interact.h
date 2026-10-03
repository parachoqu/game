#pragma once
// Tipos de alvo de uso (game/interact.js `targetsNear`). O cliente monta a mesma lista a partir do
// layout e do estado replicado (tecla F, clique, anel de seleção) e manda ReqClickUse{kind, id}; o
// servidor confere com a própria lista.
#include <cstdint>

namespace rpg::protocol {

// `id` de cada tipo:
//   Node … Observatory — índice em GameplayLayout::interactables
//   LootBag, Traveler  — EntityId do saco / do NPC
//   Portal             — WorldState::portalId
//   Mount              — 0 (a montaria do próprio jogador)
//   Shrine, Exit       — índice nos santuários / saídas do layout da Turbulenta
enum class InteractKind : std::uint8_t {
  Node, Market, Forge, Trainer, Storage, Stable, Board, Canteiro, Astronomer, Vigia, Observatory,
  LootBag, Traveler, Portal, Mount, Shrine, Exit,
};

}  // namespace rpg::protocol
