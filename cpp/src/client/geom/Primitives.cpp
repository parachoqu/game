#include "client/geom/Primitives.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_inverse.hpp>

namespace rpg::client {

void MeshData::computeBounds() {
  if (vertices.empty()) return;
  boundsMin = boundsMax = {vertices[0].px, vertices[0].py, vertices[0].pz};
  for (const Vertex& v : vertices) {
    boundsMin = glm::min(boundsMin, glm::vec3(v.px, v.py, v.pz));
    boundsMax = glm::max(boundsMax, glm::vec3(v.px, v.py, v.pz));
  }
}

void MeshData::append(const MeshData& o, const glm::mat4& m, std::uint32_t color) {
  const auto base = static_cast<std::uint32_t>(vertices.size());
  const glm::mat3 n = glm::inverseTranspose(glm::mat3(m));
  for (const Vertex& v : o.vertices) {
    const glm::vec3 p = glm::vec3(m * glm::vec4(v.px, v.py, v.pz, 1.0f));
    glm::vec3 nn = n * glm::vec3(v.nx, v.ny, v.nz);
    const float l = glm::length(nn);
    if (l > 0) nn /= l;
    vertices.push_back({p.x, p.y, p.z, nn.x, nn.y, nn.z, color});
  }
  for (std::uint32_t i : o.indices) indices.push_back(base + i);
}

Instance makeInstance(const glm::mat4& m, glm::vec4 tint) {
  Instance in{};
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r) in.m[c * 4 + r] = m[c][r];
  in.tint[0] = tint.r;
  in.tint[1] = tint.g;
  in.tint[2] = tint.b;
  in.tint[3] = tint.a;
  return in;
}

Instance makeInstance(const glm::mat4& m, std::uint32_t rgb, float alpha) {
  return makeInstance(m, glm::vec4(static_cast<float>((rgb >> 16) & 255) / 255.0f, static_cast<float>((rgb >> 8) & 255) / 255.0f,
                                   static_cast<float>(rgb & 255) / 255.0f, alpha));
}

namespace prim {

namespace {

constexpr std::uint32_t kWhite = 0xFFFFFFFFu;
constexpr float kTau = 6.28318530717958647692f;

void addQuad(MeshData& m, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n) {
  const auto base = static_cast<std::uint32_t>(m.vertices.size());
  for (const glm::vec3& p : {a, b, c, d}) m.vertices.push_back({p.x, p.y, p.z, n.x, n.y, n.z, kWhite});
  m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

}  // namespace

MeshData box() {
  MeshData m;
  const float h = 0.5f;
  addQuad(m, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}, {0, 0, 1});
  addQuad(m, {h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}, {0, 0, -1});
  addQuad(m, {h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}, {1, 0, 0});
  addQuad(m, {-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}, {-1, 0, 0});
  addQuad(m, {-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}, {0, 1, 0});
  addQuad(m, {-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}, {0, -1, 0});
  m.computeBounds();
  return m;
}

MeshData cylinder(int seg) {
  MeshData m;
  for (int i = 0; i <= seg; ++i) {
    const float a = kTau * static_cast<float>(i) / static_cast<float>(seg);
    const float x = std::sin(a), z = std::cos(a);
    m.vertices.push_back({x, 0, z, x, 0, z, kWhite});
    m.vertices.push_back({x, 1, z, x, 0, z, kWhite});
  }
  for (int i = 0; i < seg; ++i) {
    const auto b = static_cast<std::uint32_t>(i * 2);
    m.indices.insert(m.indices.end(), {b, b + 2, b + 1, b + 1, b + 2, b + 3});
  }
  for (int cap = 0; cap < 2; ++cap) {
    const float y = static_cast<float>(cap), ny = cap ? 1.0f : -1.0f;
    const auto c = static_cast<std::uint32_t>(m.vertices.size());
    m.vertices.push_back({0, y, 0, 0, ny, 0, kWhite});
    for (int i = 0; i <= seg; ++i) {
      const float a = kTau * static_cast<float>(i) / static_cast<float>(seg);
      m.vertices.push_back({std::sin(a), y, std::cos(a), 0, ny, 0, kWhite});
    }
    for (int i = 0; i < seg; ++i) {
      const auto a = c + 1 + static_cast<std::uint32_t>(i);
      if (cap) m.indices.insert(m.indices.end(), {c, a, a + 1});
      else m.indices.insert(m.indices.end(), {c, a + 1, a});
    }
  }
  m.computeBounds();
  return m;
}

MeshData cone(int seg) {
  MeshData m;
  const float slope = 1.0f / std::sqrt(2.0f);
  for (int i = 0; i < seg; ++i) {
    const float a0 = kTau * static_cast<float>(i) / static_cast<float>(seg);
    const float a1 = kTau * static_cast<float>(i + 1) / static_cast<float>(seg);
    const float am = (a0 + a1) * 0.5f;
    const auto b = static_cast<std::uint32_t>(m.vertices.size());
    m.vertices.push_back({std::sin(a0), 0, std::cos(a0), std::sin(a0) * slope, slope, std::cos(a0) * slope, kWhite});
    m.vertices.push_back({std::sin(a1), 0, std::cos(a1), std::sin(a1) * slope, slope, std::cos(a1) * slope, kWhite});
    m.vertices.push_back({0, 1, 0, std::sin(am) * slope, slope, std::cos(am) * slope, kWhite});
    m.indices.insert(m.indices.end(), {b, b + 1, b + 2});
  }
  const auto c = static_cast<std::uint32_t>(m.vertices.size());
  m.vertices.push_back({0, 0, 0, 0, -1, 0, kWhite});
  for (int i = 0; i <= seg; ++i) {
    const float a = kTau * static_cast<float>(i) / static_cast<float>(seg);
    m.vertices.push_back({std::sin(a), 0, std::cos(a), 0, -1, 0, kWhite});
  }
  for (int i = 0; i < seg; ++i) {
    const auto a = c + 1 + static_cast<std::uint32_t>(i);
    m.indices.insert(m.indices.end(), {c, a + 1, a});
  }
  m.computeBounds();
  return m;
}

MeshData sphere(int rings, int seg) {
  MeshData m;
  for (int r = 0; r <= rings; ++r) {
    const float phi = 3.14159265f * static_cast<float>(r) / static_cast<float>(rings);
    for (int s = 0; s <= seg; ++s) {
      const float th = kTau * static_cast<float>(s) / static_cast<float>(seg);
      const float x = std::sin(phi) * std::sin(th), y = std::cos(phi), z = std::sin(phi) * std::cos(th);
      m.vertices.push_back({x, y, z, x, y, z, kWhite});
    }
  }
  const auto row = static_cast<std::uint32_t>(seg + 1);
  for (int r = 0; r < rings; ++r)
    for (int s = 0; s < seg; ++s) {
      const auto a = static_cast<std::uint32_t>(r) * row + static_cast<std::uint32_t>(s);
      m.indices.insert(m.indices.end(), {a, a + row, a + 1, a + 1, a + row, a + row + 1});
    }
  m.computeBounds();
  return m;
}

MeshData capsule(double radius, double height, int rings, int seg) {
  MeshData m;
  const float R = static_cast<float>(radius), H = static_cast<float>(std::max(height, 2 * radius));
  const float mid = H - 2 * R;
  // hemisférios + cilindro: anéis de latitude, com o equador duplicado para esticar o meio
  std::vector<std::pair<float, float>> lat;  // (ângulo, deslocamento em y)
  for (int r = 0; r <= rings; ++r) lat.push_back({3.14159265f * 0.5f * static_cast<float>(r) / static_cast<float>(rings), R + mid});
  for (int r = 0; r <= rings; ++r)
    lat.push_back({3.14159265f * 0.5f + 3.14159265f * 0.5f * static_cast<float>(r) / static_cast<float>(rings), R});
  for (const auto& [phi, off] : lat)
    for (int s = 0; s <= seg; ++s) {
      const float th = kTau * static_cast<float>(s) / static_cast<float>(seg);
      const float x = std::sin(phi) * std::sin(th), y = std::cos(phi), z = std::sin(phi) * std::cos(th);
      m.vertices.push_back({x * R, y * R + off, z * R, x, y, z, kWhite});
    }
  const auto row = static_cast<std::uint32_t>(seg + 1);
  for (std::size_t r = 0; r + 1 < lat.size(); ++r)
    for (int s = 0; s < seg; ++s) {
      const auto a = static_cast<std::uint32_t>(r) * row + static_cast<std::uint32_t>(s);
      m.indices.insert(m.indices.end(), {a, a + row, a + 1, a + 1, a + row, a + row + 1});
    }
  m.computeBounds();
  return m;
}

MeshData ring(double inner, double outer, int seg) {
  MeshData m;
  for (int i = 0; i <= seg; ++i) {
    const float a = kTau * static_cast<float>(i) / static_cast<float>(seg);
    const float s = std::sin(a), c = std::cos(a);
    m.vertices.push_back({s * static_cast<float>(inner), 0, c * static_cast<float>(inner), 0, 1, 0, kWhite});
    m.vertices.push_back({s * static_cast<float>(outer), 0, c * static_cast<float>(outer), 0, 1, 0, kWhite});
  }
  for (int i = 0; i < seg; ++i) {
    const auto b = static_cast<std::uint32_t>(i * 2);
    m.indices.insert(m.indices.end(), {b, b + 1, b + 2, b + 1, b + 3, b + 2});
  }
  m.computeBounds();
  return m;
}

}  // namespace prim
}  // namespace rpg::client
