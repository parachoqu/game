#pragma once
// Tipos básicos compartilhados por servidor e cliente.
//
// A simulação usa double, como o JS da demo: as mesmas contas dão os mesmos resultados (paridade com
// as fixtures exportadas de demo/) e as posições ficam exatas até muito além dos 1.400 m do mundo.
// O render converte para float na hora de desenhar.
#include <cstdint>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace rpg {

using Real = double;
using Vec2 = glm::dvec2;  // plano do chão: x, z
using Vec3 = glm::dvec3;  // cena: x, y (altura), z — norte = -z, como no three.js da demo

using Tick = std::uint64_t;  // número do passo fixo da simulação
using Seconds = double;

}  // namespace rpg
