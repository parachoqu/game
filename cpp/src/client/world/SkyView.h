#pragma once
// O que o céu desenha além da atmosfera (engine/sky.js): estrelas com o sumiço das apagadas,
// linhas das constelações (na observação), lua e o anel da lacuna observada. Sem SDL; testável.
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "client/geom/MeshData.h"
#include "core/world/SkyLayout.h"

namespace rpg::client {

struct StarInstance {
  float pos[4];   // xyz: direção × R; w: tamanho
  float attr[4];  // x: brilho; y: fase; z: sumiço (aGone)
};
static_assert(sizeof(StarInstance) == 32);

struct SkyFrame {
  std::vector<StarInstance> stars;
  std::vector<ColorVertex> lines;    // segmentos das constelações (LINELIST), no mundo
  std::vector<ColorVertex> overlay;  // lua e anel da lacuna (triângulos sem luz), no mundo
  float starNight = 0;               // uNight
  float lineOpacity = 0;
};

struct SkyInput {
  glm::vec3 camPos{0.0f};
  glm::vec3 sunDir{0.0f, 1.0f, 0.0f};
  bool turbulent = false;
  double night = 0;
  double time = 0;                        // G.time (ou o tempo da tela de título)
  struct Gone {
    int index = 0;
    double since = 0;                     // tempo (em `time`) em que começou a sumir
  };
  std::vector<Gone> vanished;
  float lineOpacity = 0;                  // 0,35 durante a observação
  std::optional<int> gapStar;             // anel na lacuna observada
};

class SkyView {
 public:
  SkyView();
  const SkyLayout& layout() const { return layout_; }
  void build(const SkyInput& in, SkyFrame& out) const;

  // stars.js: valor de aGone `t` segundos depois de começar a sumir (cintila 3 s, depois some).
  static float goneAt(double t);

 private:
  SkyLayout layout_;
  std::vector<std::array<int, 2>> lineStars_;
  std::vector<glm::vec3> moonVerts_;  // esfera unitária em triângulos
};

}  // namespace rpg::client
