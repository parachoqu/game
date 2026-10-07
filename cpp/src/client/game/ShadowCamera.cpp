#include "client/game/ShadowCamera.h"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace rpg::client {

ShadowCamera sunShadowCamera(glm::vec3 toLight, glm::vec3 focus, float span, float far, int size) {
  ShadowCamera c;
  const glm::vec3 dir = glm::normalize(toLight);
  const glm::vec3 up = std::abs(dir.y) > 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
  c.view = glm::lookAtRH(focus + dir * 160.0f, focus, up);
  c.proj = glm::orthoRH_ZO(-span, span, -span, span, 1.0f, far);
  // encaixe no texel: a origem do mundo cai num canto de texel, então tudo cai
  const glm::vec4 o = c.proj * c.view * glm::vec4(0, 0, 0, 1);
  const float half = static_cast<float>(size) * 0.5f;
  const glm::vec2 t(o.x * half, o.y * half);
  const glm::vec2 snap = (glm::round(t) - t) / half;
  c.proj[3][0] += snap.x;
  c.proj[3][1] += snap.y;
  c.viewProj = c.proj * c.view;
  c.texelWorld = 2.0f * span / static_cast<float>(size);
  return c;
}

}  // namespace rpg::client
