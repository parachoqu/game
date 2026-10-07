#include "client/world/SkyView.h"

#include <cmath>
#include <unordered_map>

namespace rpg::client {

namespace {

constexpr float kPi = 3.14159265358979f;

// THREE.SphereGeometry(1, 16, 12) em triângulos
std::vector<glm::vec3> unitSphere(int ws, int hs) {
  std::vector<std::vector<glm::vec3>> grid;
  for (int iy = 0; iy <= hs; ++iy) {
    std::vector<glm::vec3> row;
    const float v = static_cast<float>(iy) / static_cast<float>(hs);
    for (int ix = 0; ix <= ws; ++ix) {
      const float u = static_cast<float>(ix) / static_cast<float>(ws);
      row.push_back({-std::cos(u * 2 * kPi) * std::sin(v * kPi), std::cos(v * kPi), std::sin(u * 2 * kPi) * std::sin(v * kPi)});
    }
    grid.push_back(row);
  }
  std::vector<glm::vec3> tris;
  for (int iy = 0; iy < hs; ++iy)
    for (int ix = 0; ix < ws; ++ix) {
      const auto& a = grid[static_cast<std::size_t>(iy)][static_cast<std::size_t>(ix + 1)];
      const auto& b = grid[static_cast<std::size_t>(iy)][static_cast<std::size_t>(ix)];
      const auto& c = grid[static_cast<std::size_t>(iy + 1)][static_cast<std::size_t>(ix)];
      const auto& d = grid[static_cast<std::size_t>(iy + 1)][static_cast<std::size_t>(ix + 1)];
      if (iy != 0) tris.insert(tris.end(), {a, b, d});
      if (iy != hs - 1) tris.insert(tris.end(), {b, c, d});
    }
  return tris;
}

}  // namespace

SkyView::SkyView() : layout_(SkyLayout::generate()) {
  for (const Constellation& c : layout_.constellations)
    for (const auto& [a, b] : c.lines) lineStars_.push_back({c.indices[static_cast<std::size_t>(a)], c.indices[static_cast<std::size_t>(b)]});
  moonVerts_ = unitSphere(16, 12);
}

float SkyView::goneAt(double t) {
  if (t < 0) return 0.0f;
  if (t >= 3) return 1.0f;
  return static_cast<float>((std::sin(t * 22) * 0.5 + 0.5) * (t / 3));
}

void SkyView::build(const SkyInput& in, SkyFrame& out) const {
  out.stars.clear();
  out.lines.clear();
  out.overlay.clear();
  std::unordered_map<int, float> gone;
  for (const SkyInput::Gone& g : in.vanished) gone[g.index] = goneAt(in.time - g.since);
  out.stars.reserve(layout_.stars.size());
  for (std::size_t i = 0; i < layout_.stars.size(); ++i) {
    const Star& s = layout_.stars[i];
    StarInstance si{};
    si.pos[0] = static_cast<float>(s.x);
    si.pos[1] = static_cast<float>(s.y);
    si.pos[2] = static_cast<float>(s.z);
    si.pos[3] = static_cast<float>(s.size);
    si.attr[0] = static_cast<float>(s.bright);
    si.attr[1] = static_cast<float>(s.phase);
    const auto it = gone.find(static_cast<int>(i));
    si.attr[2] = it == gone.end() ? 0.0f : it->second;
    out.stars.push_back(si);
  }
  out.starNight = in.turbulent ? 0.25f : std::max(0.04f, static_cast<float>(in.night));
  out.lineOpacity = in.lineOpacity;

  const auto starAt = [&](int i) {
    const Star& s = layout_.stars[static_cast<std::size_t>(i)];
    return in.camPos + glm::vec3(static_cast<float>(s.x), static_cast<float>(s.y), static_cast<float>(s.z));
  };
  if (in.lineOpacity > 0) {
    const std::uint32_t c = packHex(0x9fd0ff, static_cast<std::uint8_t>(std::lround(in.lineOpacity * 255.0f)));
    for (const auto& [a, b] : lineStars_) {
      const glm::vec3 p = starAt(a), q = starAt(b);
      out.lines.push_back({p.x, p.y, p.z, c});
      out.lines.push_back({q.x, q.y, q.z, c});
    }
  }
  // lua: esfera r38 a 1.300 m, do lado oposto ao sol (só à noite e fora da Turbulenta)
  if (!in.turbulent && in.night > 0.2) {
    const glm::vec3 center = in.camPos - glm::normalize(in.sunDir) * 1300.0f;
    const std::uint32_t c = packHex(0xe8e4d8);
    for (const glm::vec3& v : moonVerts_) {
      const glm::vec3 p = center + v * 38.0f;
      out.overlay.push_back({p.x, p.y, p.z, c});
    }
  }
  // anel da lacuna (RingGeometry(34, 40, 40), voltado para a câmera)
  if (in.gapStar) {
    const glm::vec3 d = glm::normalize(starAt(*in.gapStar) - in.camPos);
    const glm::vec3 center = in.camPos + d * static_cast<float>(SkyLayout::kRadius - 20);
    const glm::vec3 a = glm::normalize(std::abs(d.y) < 0.99f ? glm::cross(d, glm::vec3(0, 1, 0)) : glm::cross(d, glm::vec3(1, 0, 0)));
    const glm::vec3 b = glm::cross(d, a);
    const std::uint32_t c = packHex(0xf2c86a, static_cast<std::uint8_t>(std::lround(0.9 * 255)));
    for (int i = 0; i < 40; ++i) {
      const float t0 = static_cast<float>(i) / 40.0f * 2 * kPi, t1 = static_cast<float>(i + 1) / 40.0f * 2 * kPi;
      const auto at = [&](float r, float t) { const glm::vec3 p = center + (a * std::cos(t) + b * std::sin(t)) * r; return ColorVertex{p.x, p.y, p.z, c}; };
      const ColorVertex i0 = at(34, t0), o0 = at(40, t0), i1 = at(34, t1), o1 = at(40, t1);
      out.overlay.insert(out.overlay.end(), {i0, o0, o1, i0, o1, i1});
    }
  }
}

}  // namespace rpg::client
