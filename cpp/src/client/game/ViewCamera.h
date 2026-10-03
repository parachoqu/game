#pragma once
// Câmera de perspectiva (THREE.PerspectiveCamera): matrizes, projeção de pontos na tela e raio do
// cursor. glm com Y para cima, mão direita, profundidade 0–1 (a convenção do SDL_GPU).
#include <optional>

#include <glm/glm.hpp>

namespace rpg::client {

struct Ray {
  glm::dvec3 origin{0.0};
  glm::dvec3 dir{0.0, 0.0, -1.0};
};

struct ViewCamera {
  glm::dvec3 position{0.0, 10.0, 10.0};
  glm::dvec3 target{0.0};
  double fovDeg = 55.0;
  double near = 0.1, far = 2400.0;
  double aspect = 16.0 / 9.0;

  glm::dmat4 view() const;
  glm::dmat4 projection() const;
  glm::dmat4 viewProj() const { return projection() * view(); }

  // Ponto do mundo → pixels (origem no canto superior esquerdo). Vazio atrás da câmera
  // (pointer.js `screenOf`: v.z > 1).
  std::optional<glm::dvec2> project(const glm::dvec3& world, double width, double height) const;
  // Raio a partir de um pixel (THREE.Raycaster.setFromCamera).
  Ray ray(double px, double py, double width, double height) const;
};

}  // namespace rpg::client
