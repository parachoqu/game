#pragma once
// Malha do chão de um mapa (world/scene-world.js): blocos de 64 m com duas resoluções (2 m perto,
// 8 m longe na região; 1 m e 4 m na Turbulenta), cor por camada de solo, lâmina do rio e tabuleiro
// das pontes. As alturas vêm do mesmo MapTerrain que a simulação usa: o chão desenhado coincide com o
// chão calculado.
#include <vector>

#include <glm/glm.hpp>

#include "client/geom/MeshData.h"

namespace rpg {
class StaticMap;
}

namespace rpg::client {

struct TerrainChunk {
  MeshData lod[2];
  glm::vec3 center{0.0f};
  float radius = 0;
};

struct TerrainBuild {
  std::vector<TerrainChunk> chunks;
  MeshData water;    // região: lâmina do rio
  MeshData bridges;  // tabuleiros das pontes
  double half = 0;   // meia extensão do mapa
  glm::dvec2 center{0.0};
};

// Meia extensão do relevo do mapa (512 m na região, 128 m na Turbulenta).
double mapHalfExtent(const StaticMap& map);
TerrainBuild buildTerrain(const StaticMap& map);

// Cor sRGB do chão em (x, z): bioma, inclinação, estrada, água (aproximação das camadas da demo).
std::uint32_t groundColor(const StaticMap& map, double x, double z, double ny);

}  // namespace rpg::client
