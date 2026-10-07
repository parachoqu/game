#include "client/game/ViewCamera.h"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace rpg::client {

glm::dmat4 ViewCamera::view() const {
  glm::dvec3 t = target;
  if (glm::length(t - position) < 1e-9) t = position + glm::dvec3(0, 0, -1);
  // olhando reto para cima/baixo, o "para cima" muda para não degenerar
  const glm::dvec3 f = glm::normalize(t - position);
  const glm::dvec3 up = std::abs(f.y) > 0.999 ? glm::dvec3(0, 0, -1) : glm::dvec3(0, 1, 0);
  return glm::lookAtRH(position, t, up);
}

glm::dmat4 ViewCamera::projection() const {
  return glm::perspectiveRH_ZO(glm::radians(fovDeg), aspect, near, far);
}

std::optional<glm::dvec2> ViewCamera::project(const glm::dvec3& world, double width, double height) const {
  const glm::dvec4 c = viewProj() * glm::dvec4(world, 1.0);
  if (c.w <= 1e-6) return std::nullopt;
  const glm::dvec3 ndc = glm::dvec3(c) / c.w;
  if (ndc.z > 1.0) return std::nullopt;
  return glm::dvec2((ndc.x * 0.5 + 0.5) * width, (-ndc.y * 0.5 + 0.5) * height);
}

Ray ViewCamera::ray(double px, double py, double width, double height) const {
  const double nx = px / width * 2.0 - 1.0;
  const double ny = -(py / height) * 2.0 + 1.0;
  const glm::dmat4 inv = glm::inverse(viewProj());
  glm::dvec4 a = inv * glm::dvec4(nx, ny, 0.0, 1.0);
  glm::dvec4 b = inv * glm::dvec4(nx, ny, 1.0, 1.0);
  a /= a.w;
  b /= b.w;
  Ray r;
  r.origin = position;
  r.dir = glm::normalize(glm::dvec3(b) - glm::dvec3(a));
  return r;
}

}  // namespace rpg::client
