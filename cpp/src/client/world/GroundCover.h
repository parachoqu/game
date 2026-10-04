#pragma once
// Cobertura do chão (world/ground-cover.js): grama, flores, cogumelos, seixos, galhos e grama de campo
// em células de 32 m em volta da câmera.
//
// A demo gera cada célula na hora (`generateCell`, com semente própria); aqui o bake visual já rodou
// essa mesma função para as 1.024 células (PackGroundCover), e o cliente só escolhe as células no
// alcance, decide perto/longe (26 m) e corta cada lista pela distância e pela densidade do nível
// (`show`): como a lista vem em ordem aleatória, cortar do fim rareia por igual.
#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "client/assets/ScenePack.h"
#include "client/geom/MeshData.h"

namespace rpg::client {

struct PackFrame;

class GroundCover {
 public:
  explicit GroundCover(const ScenePack& pack);

  bool present() const { return present_; }
  std::size_t cellCount() const { return cells_.size(); }

  // Acrescenta ao quadro as células dentro de `radius` (COVER.radius) com a densidade do nível
  // (COVER.density). `viewProj` recorta por célula.
  void update(glm::vec3 cam, const glm::mat4& viewProj, float radius, float density, PackFrame& out);

  // A instância de uma entrada (matriz e cor), como `fill` monta (testes).
  struct Item {
    float x = 0, y = 0, z = 0, s = 1, sy = 1, rot = 0;
    glm::vec3 color{1.0f};
  };
  Item item(std::size_t cell, std::size_t kind, std::uint32_t index) const;
  static Instance instanceOf(const Item& it);

  // Quantas instâncias de cada tipo uma célula mostra a esta distância (ground-cover.js `show`).
  static std::uint32_t visibleCount(std::uint32_t n, float dist, float radius, float density);

 private:
  struct Cell {
    int i = 0, j = 0;
    float cx = 0, cz = 0;
    glm::vec3 center{0.0f};
    float radius = 32;
    const PackCoverCell* src = nullptr;
  };
  const std::vector<Instance>& instances(std::size_t cell);

  const ScenePack& pack_;
  bool present_ = false;
  std::vector<std::uint8_t> items_;  // 16 bytes por instância
  std::vector<Cell> cells_;
  // instâncias montadas por célula (as mais recentes ficam; ~3 vezes as células do alcance)
  std::unordered_map<std::size_t, std::vector<Instance>> cache_;
  std::list<std::size_t> lru_;
};

}  // namespace rpg::client
