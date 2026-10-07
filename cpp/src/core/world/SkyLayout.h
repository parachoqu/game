#pragma once
// Campo de estrelas e constelações (engine/sky.js `createSky`, a parte que não desenha nada).
//
// O céu da demo é gerado com semente fixa (mulberry 42): as mesmas 1.740 estrelas em todo boot. A
// simulação precisa só da ordem de desaparecimento (índice da estrela e constelação); o cliente usa as
// direções, tamanhos e brilhos para desenhar. Os dois geram o mesmo campo a partir deste código.
#include <array>
#include <string>
#include <vector>

namespace rpg {

struct Star {
  double x = 0, y = 0, z = 0;  // direção unitária × raio da cúpula (R = 1500)
  double size = 1, bright = 1, phase = 0;
};

struct Constellation {
  std::string key;  // nome em data/text (Lanterna, Âncora, Cervo, Balança)
  double az = 0, el = 0;
  std::vector<std::array<double, 2>> pts;
  std::vector<std::array<int, 2>> lines;
  std::vector<int> indices;  // estrelas da constelação
};

struct VanishStep {
  int index = 0;          // estrela
  int constellation = 0;  // -1 = ponto isolado
};

struct SkyLayout {
  static constexpr double kRadius = 1500.0;
  std::vector<Star> stars;
  std::vector<Constellation> constellations;
  int titleStar = 0;            // reservada para a tela de título
  std::vector<int> isolated;    // 12 pontos isolados
  std::vector<VanishStep> order;

  // Direção unitária da estrela `index` (sky.starDir).
  std::array<double, 3> dir(int index) const;

  static SkyLayout generate();
};

}  // namespace rpg
