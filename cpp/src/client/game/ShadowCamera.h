#pragma once
// Câmera da sombra do sol (engine/sky.js + renderer.js): a luz fica 160 m acima do jogador na direção
// do sol, olhando para ele, com projeção ortográfica de ±`span` m e profundidade de 1 a `far` m (o
// volume acompanha o jogador, como `sun.position = playerPos + lightDir·160`).
//
// O centro é encaixado na grade de texels do mapa: andando, a sombra desliza de texel em texel em vez
// de tremer na borda.
#include <glm/glm.hpp>

namespace rpg::client {

struct ShadowCamera {
  glm::mat4 view{1.0f}, proj{1.0f}, viewProj{1.0f};
  float texelWorld = 0;  // lado de um texel no mundo (m)
};

// `toLight`: direção (unitária) de onde vem a luz; `size`: lado do mapa em texels.
ShadowCamera sunShadowCamera(glm::vec3 toLight, glm::vec3 focus, float span, float far, int size);

}  // namespace rpg::client
