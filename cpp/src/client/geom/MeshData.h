#pragma once
// Geometria na CPU, no formato que os shaders de cena leem (scene.vert): posição, normal e cor sRGB.
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace rpg::client {

struct Vertex {
  float px, py, pz;
  float nx, ny, nz;
  std::uint32_t color;  // RGBA8 sRGB (r no byte baixo)
};
static_assert(sizeof(Vertex) == 28);

// Instância de malha: matriz do modelo (coluna a coluna) e cor que multiplica a do vértice.
struct Instance {
  float m[16];
  float tint[4];
};
static_assert(sizeof(Instance) == 80);

// Vértice sem luz (telegrafias, anéis, barras, partículas).
struct ColorVertex {
  float x, y, z;
  std::uint32_t color;
};
static_assert(sizeof(ColorVertex) == 16);

struct MeshData {
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;
  glm::vec3 boundsMin{0.0f}, boundsMax{0.0f};

  void computeBounds();
  // Acrescenta outra malha transformada por `m` (normais pela inversa transposta).
  void append(const MeshData& other, const glm::mat4& m, std::uint32_t color);
};

inline std::uint32_t packRgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
  return static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8) | (static_cast<std::uint32_t>(b) << 16) |
         (static_cast<std::uint32_t>(a) << 24);
}
inline std::uint32_t packHex(std::uint32_t rgb, std::uint8_t a = 255) {
  return packRgba(static_cast<std::uint8_t>(rgb >> 16), static_cast<std::uint8_t>(rgb >> 8), static_cast<std::uint8_t>(rgb), a);
}

Instance makeInstance(const glm::mat4& m, glm::vec4 tint = glm::vec4(1.0f));
Instance makeInstance(const glm::mat4& m, std::uint32_t rgb, float alpha = 1.0f);

}  // namespace rpg::client
