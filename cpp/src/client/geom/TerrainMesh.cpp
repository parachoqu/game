#include "client/geom/TerrainMesh.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "client/geom/Primitives.h"
#include "core/world/StaticWorld.h"

namespace rpg::client {

namespace {

// Camadas de solo (scene-world.js: 0 grama · 1 grama seca · 2 rocha · 3 terra · 4 areia · 5 neve ·
// 6 cinza · 7 calçamento · 8 solo de bosque · 9 seixos), cor média de cada textura.
constexpr glm::vec3 kLayer[10] = {
    {0.40f, 0.49f, 0.25f}, {0.56f, 0.53f, 0.33f}, {0.49f, 0.47f, 0.43f}, {0.49f, 0.39f, 0.28f}, {0.72f, 0.65f, 0.49f},
    {0.90f, 0.91f, 0.92f}, {0.36f, 0.35f, 0.34f}, {0.56f, 0.54f, 0.50f}, {0.31f, 0.30f, 0.19f}, {0.52f, 0.49f, 0.44f},
};
constexpr glm::vec3 kTurbGround{0.23f, 0.20f, 0.27f};

double sstep(double e0, double e1, double x) {
  const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
  return t * t * (3 - 2 * t);
}

std::uint32_t pack(glm::vec3 c) {
  const auto b = [](float v) { return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
  return packRgba(b(c.r), b(c.g), b(c.b));
}

glm::vec3 normalAt(const StaticMap& map, double x, double z, double d) {
  const MapTerrain& t = map.terrain();
  const double hl = t.terrainHeight(x - d, z), hr = t.terrainHeight(x + d, z);
  const double hd = t.terrainHeight(x, z - d), hu = t.terrainHeight(x, z + d);
  return glm::normalize(glm::vec3(static_cast<float>(hl - hr), static_cast<float>(2 * d), static_cast<float>(hd - hu)));
}

}  // namespace

double mapHalfExtent(const StaticMap& map) {
  const auto& m = map.terrain().heightfield().meta();
  return (m.w - 1) * m.step * 0.5;
}

std::uint32_t groundColor(const StaticMap& map, double x, double z, double ny) {
  const MapTerrain& t = map.terrain();
  const double h = t.terrainHeight(x, z);
  if (map.kind() == MapKind::Turbulent) {
    glm::vec3 c = kTurbGround;
    c = glm::mix(c, kLayer[3] * 0.7f, static_cast<float>(sstep(0.1, 0.55, std::fmod(h, 7.0) / 7.0) * 0.45));
    if (ny < 0.86) c = glm::mix(c, kLayer[2] * 0.8f, static_cast<float>(std::min(1.0, (0.86 - ny) * 5)));
    return pack(c);
  }
  float w[10] = {};
  const auto toward = [&](int k, double f) {
    if (f <= 0) return;
    f = std::min(f, 1.0);
    for (float& v : w) v *= static_cast<float>(1 - f);
    w[k] += static_cast<float>(f);
  };
  const int biome = t.biomeAt(x, z);
  if (biome == 2) w[2] = 1;
  else if (biome == 3) w[8] = 1;
  else if (biome == 4) w[1] = 1;
  else if (biome == 5) w[9] = 1;
  else w[0] = 1;
  if (biome == 1) toward(8, 0.45);
  if (ny < 0.88) toward(2, (0.88 - ny) * 5);
  if (h > 86) toward(2, (h - 86) / 16);
  if (t.isWaterCell(x, z)) toward(9, 0.85);
  if (map.hasRouteField()) {
    const double rd = map.routeDistance(x, z);
    if (rd < 7) toward(3, 0.92 * (1 - sstep(2.6, 7, rd)));
  }
  glm::vec3 c(0.0f);
  for (int i = 0; i < 10; ++i) c += kLayer[i] * w[i];
  return pack(c);
}

TerrainBuild buildTerrain(const StaticMap& map) {
  TerrainBuild out;
  const MapTerrain& t = map.terrain();
  const double half = mapHalfExtent(map);
  const XZ center = map.center();
  out.half = half;
  out.center = {center.x, center.z};
  const bool region = map.kind() == MapKind::Region;
  const double steps[2] = {region ? 2.0 : 1.0, region ? 8.0 : 4.0};
  constexpr double kSub = 64;
  const int n = static_cast<int>(std::round(2 * half / kSub));
  for (int cj = 0; cj < n; ++cj) {
    for (int ci = 0; ci < n; ++ci) {
      const double x0 = center.x - half + ci * kSub, z0 = center.z - half + cj * kSub;
      TerrainChunk ch;
      for (int l = 0; l < 2; ++l) {
        const double st = steps[l];
        const int cells = static_cast<int>(kSub / st);
        MeshData& m = ch.lod[l];
        m.vertices.reserve(static_cast<std::size_t>((cells + 1) * (cells + 1)));
        for (int j = 0; j <= cells; ++j) {
          for (int i = 0; i <= cells; ++i) {
            const double x = x0 + i * st, z = z0 + j * st;
            const double y = t.terrainHeight(x, z);
            const glm::vec3 nrm = normalAt(map, x, z, std::max(1.0, st * 0.5));
            m.vertices.push_back({static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), nrm.x, nrm.y, nrm.z,
                                  groundColor(map, x, z, static_cast<double>(nrm.y))});
          }
        }
        const auto row = static_cast<std::uint32_t>(cells + 1);
        for (int j = 0; j < cells; ++j)
          for (int i = 0; i < cells; ++i) {
            const auto a = static_cast<std::uint32_t>(j) * row + static_cast<std::uint32_t>(i);
            // mesma diagonal da amostragem bilinear do relevo (a–d)
            m.indices.insert(m.indices.end(), {a, a + row, a + row + 1, a, a + row + 1, a + 1});
          }
        // saia para baixo nas bordas: esconde as frestas entre blocos de resoluções diferentes
        const auto skirt = [&](std::uint32_t ia, std::uint32_t ib) {
          const Vertex va = m.vertices[ia], vb = m.vertices[ib];
          const auto base = static_cast<std::uint32_t>(m.vertices.size());
          Vertex da = va, db = vb;
          da.py -= static_cast<float>(st);
          db.py -= static_cast<float>(st);
          m.vertices.push_back(da);
          m.vertices.push_back(db);
          m.indices.insert(m.indices.end(), {ia, base, ib, ib, base, base + 1});
        };
        for (std::uint32_t i = 0; i < row - 1; ++i) {
          skirt(i + 1, i);
          skirt((row - 1) * row + i, (row - 1) * row + i + 1);
          skirt(i * row, (i + 1) * row);
          skirt((i + 1) * row + row - 1, i * row + row - 1);
        }
        m.computeBounds();
      }
      ch.center = (ch.lod[0].boundsMin + ch.lod[0].boundsMax) * 0.5f;
      ch.radius = glm::length(ch.lod[0].boundsMax - ch.lod[0].boundsMin) * 0.5f;
      out.chunks.push_back(std::move(ch));
    }
  }

  if (region) {
    // lâmina do rio: células de 2 m marcadas como água, na cota do rio naquela coluna
    const River& river = t.river();
    const std::uint32_t water = packRgba(0x2f, 0x5c, 0x66, 200);
    constexpr double kCell = 2;
    for (double z = center.z - half; z < center.z + half; z += kCell) {
      for (double x = center.x - half; x < center.x + half; x += kCell) {
        if (!t.isWaterCell(x + kCell * 0.5, z + kCell * 0.5)) continue;
        const auto base = static_cast<std::uint32_t>(out.water.vertices.size());
        for (const auto& [dx, dz] : {std::pair{0.0, 0.0}, {kCell, 0.0}, {kCell, kCell}, {0.0, kCell}}) {
          const double wx = x + dx, wz = z + dz;
          out.water.vertices.push_back({static_cast<float>(wx), static_cast<float>(river.levelAt(wx)), static_cast<float>(wz), 0, 1, 0, water});
        }
        out.water.indices.insert(out.water.indices.end(), {base, base + 3, base + 2, base, base + 2, base + 1});
      }
    }
    out.water.computeBounds();

    const MeshData cube = prim::box();
    for (const Bridge& b : t.bridges()) {
      glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(static_cast<float>(b.x), static_cast<float>(b.y) - 0.2f, static_cast<float>(b.z)));
      m = glm::rotate(m, static_cast<float>(b.rot), glm::vec3(0, 1, 0));
      m = glm::scale(m, glm::vec3(static_cast<float>(b.hw * 2), 0.4f, static_cast<float>(b.hd * 2)));
      out.bridges.append(cube, m, packHex(0x7a5c3e));
    }
    out.bridges.computeBounds();
  }
  return out;
}

}  // namespace rpg::client
