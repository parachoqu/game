#pragma once
// Luz do dia (engine/sky.js `applyDaylight`): cor do céu, sol e lua, hemisfério, névoa e ambiente
// para uma hora do dia. Cores lineares, como THREE.Color guarda.
#include <glm/glm.hpp>

#include "core/GameClock.h"

namespace rpg::client {

struct SceneLighting {
  glm::vec3 skyTop{0.0f}, skyHorizon{0.0f};
  glm::vec3 sunDir{0.0f, 1.0f, 0.0f};  // para onde está o sol (o céu desenha o disco)
  glm::vec3 lightDir{0.0f, 1.0f, 0.0f};  // de onde vem a luz direcional (luar à noite)
  glm::vec3 sunColor{1.0f};
  float sunIntensity = 1;
  glm::vec3 hemiSky{1.0f}, hemiGround{0.3f};
  float hemiIntensity = 0.5f;
  glm::vec3 fogColor{0.5f};
  float fogDensity = 0.0011f;
  float sunAmount = 1;   // disco do sol visível
  float envIntensity = 0.5f;
  Daylight daylight;
};

SceneLighting lightingAt(double hour, bool turbulent);

// "#rrggbb" sRGB → linear (THREE.Color com gerenciamento de cor).
glm::vec3 srgbHexToLinear(std::uint32_t rgb);

}  // namespace rpg::client
