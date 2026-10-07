#pragma once
// Deslocamento de uma entidade no chão (game/collide.js `moveEntity`), compartilhado pelo servidor
// (autoridade) e pelo cliente (predição do próprio jogador):
//
//   1. tenta o passo inteiro; se a encosta for íngreme demais, tenta só x e depois só z;
//   2. na estrada a rampa pode ser mais íngreme (as rotas foram feitas para ser subidas);
//   3. quem está dentro da água sempre consegue sair para a margem;
//   4. empurra o corpo para fora dos colisores e mantém a entidade dentro dos limites do mapa.
#include "core/Types.h"
#include "core/world/StaticWorld.h"

namespace rpg {

// `radius`: raio do corpo. `lift`: altura do corpo acima do chão (no pulo) — o que ele alcança
// não conta como encosta.
void moveEntity(const StaticMap& map, XZ& pos, double dx, double dz, double radius = 0.45, double lift = 0.0);

}  // namespace rpg
