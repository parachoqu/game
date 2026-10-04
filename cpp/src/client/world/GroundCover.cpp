#include "client/world/GroundCover.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "client/world/PackScene.h"

namespace rpg::client {

namespace {

constexpr float kTwoPi = 6.28318530718f;
constexpr std::size_t kCacheCells = 96;

float smoothstep(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

bool sphereInFrustum(const glm::mat4& m, glm::vec3 c, float r) {
  const auto row = [&](int i) { return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]); };
  const glm::vec4 planes[6] = {row(3) + row(0), row(3) - row(0), row(3) + row(1), row(3) - row(1), row(2), row(3) - row(2)};
  for (glm::vec4 p : planes) {
    p /= glm::length(glm::vec3(p));
    if (glm::dot(glm::vec3(p), c) + p.w < -r) return false;
  }
  return true;
}

}  // namespace

GroundCover::GroundCover(const ScenePack& pack) : pack_(pack) {
  const PackGroundCover& G = pack.groundCover;
  if (!G.present || G.count == 0) return;
  items_ = pack.decodeVertices(G.items);
  if (items_.size() < static_cast<std::size_t>(G.count) * 16) return;
  const auto cellSize = static_cast<float>(G.cell);
  for (const PackCoverCell& c : G.cells) {
    Cell cell;
    cell.i = c.i;
    cell.j = c.j;
    cell.src = &c;
    cell.cx = static_cast<float>(G.origin) + (static_cast<float>(c.i) + 0.5f) * cellSize;
    cell.cz = static_cast<float>(G.origin) + (static_cast<float>(c.j) + 0.5f) * cellSize;
    // esfera da célula pela faixa de alturas (fill: centro na média, raio com 1,5 m de folga)
    float y0 = 1e30f, y1 = -1e30f;
    for (const PackCoverCell::Slot& s : c.kinds) {
      for (std::uint32_t k = 0; k < s.count; ++k) {
        float y = 0;
        std::memcpy(&y, items_.data() + (static_cast<std::size_t>(s.first) + k) * 16 + 4, 4);
        y0 = std::min(y0, y);
        y1 = std::max(y1, y);
      }
    }
    if (y0 > y1) y0 = y1 = 0;
    cell.center = glm::vec3(cell.cx, (y0 + y1) * 0.5f + 0.5f, cell.cz);
    cell.radius = std::hypot(cellSize * 0.72f, (y1 - y0) * 0.5f + 1.5f);
    cells_.push_back(cell);
  }
  present_ = true;
}

GroundCover::Item GroundCover::item(std::size_t cell, std::size_t kind, std::uint32_t index) const {
  const PackGroundCover& G = pack_.groundCover;
  const Cell& c = cells_[cell];
  const std::uint8_t* p = items_.data() + (static_cast<std::size_t>(c.src->kinds[kind].first) + index) * 16;
  std::uint16_t qx = 0, qz = 0, qs = 0, qsy = 0;
  std::memcpy(&qx, p, 2);
  std::memcpy(&qz, p + 2, 2);
  Item it;
  std::memcpy(&it.y, p + 4, 4);
  std::memcpy(&qs, p + 8, 2);
  std::memcpy(&qsy, p + 10, 2);
  const auto cellSize = static_cast<float>(G.cell);
  it.x = static_cast<float>(G.origin) + static_cast<float>(c.i) * cellSize + static_cast<float>(qx) / 65535.0f * cellSize;
  it.z = static_cast<float>(G.origin) + static_cast<float>(c.j) * cellSize + static_cast<float>(qz) / 65535.0f * cellSize;
  it.s = static_cast<float>(qs) / 1000.0f;
  it.sy = static_cast<float>(qsy) / 1000.0f;
  it.rot = static_cast<float>(p[12]) / 256.0f * kTwoPi;
  it.color = glm::vec3(p[13], p[14], p[15]) / 127.5f;
  return it;
}

// fill: Euler(0, rot, 0) → compose(posição, giro, escala (s, sy, s))
Instance GroundCover::instanceOf(const Item& it) {
  const float c = std::cos(it.rot), s = std::sin(it.rot);
  const glm::mat4 m(glm::vec4(c * it.s, 0, -s * it.s, 0), glm::vec4(0, it.sy, 0, 0), glm::vec4(s * it.s, 0, c * it.s, 0),
                    glm::vec4(it.x, it.y, it.z, 1));
  return makeInstance(m, glm::vec4(it.color, 1.0f));
}

std::uint32_t GroundCover::visibleCount(std::uint32_t n, float dist, float radius, float density) {
  const float thin = (1.0f - 0.55f * smoothstep(30.0f, radius, dist)) * density;
  return static_cast<std::uint32_t>(std::floor(static_cast<float>(n) * std::max(0.0f, thin)));
}

const std::vector<Instance>& GroundCover::instances(std::size_t cell) {
  if (const auto it = cache_.find(cell); it != cache_.end()) {
    lru_.remove(cell);
    lru_.push_front(cell);
    return it->second;
  }
  std::vector<Instance> all;
  const Cell& c = cells_[cell];
  for (std::size_t k = 0; k < c.src->kinds.size(); ++k)
    for (std::uint32_t n = 0; n < c.src->kinds[k].count; ++n) all.push_back(instanceOf(item(cell, k, n)));
  lru_.push_front(cell);
  if (lru_.size() > kCacheCells) {
    cache_.erase(lru_.back());
    lru_.pop_back();
  }
  return cache_.emplace(cell, std::move(all)).first->second;
}

void GroundCover::update(glm::vec3 cam, const glm::mat4& viewProj, float radius, float density, PackFrame& out) {
  if (!present_ || density <= 0) return;
  const PackGroundCover& G = pack_.groundCover;
  const auto cellSize = static_cast<float>(G.cell);
  for (std::size_t ci = 0; ci < cells_.size(); ++ci) {
    const Cell& c = cells_[ci];
    const float d = std::max(0.0f, std::hypot(c.cx - cam.x, c.cz - cam.z) - cellSize * 0.71f);
    if (d > radius) continue;
    if (!sphereInFrustum(viewProj, c.center, c.radius)) continue;
    const bool near = d < 26.0f;
    const std::vector<Instance>& all = instances(ci);
    std::uint32_t base = 0;  // início do tipo em `all`
    for (std::size_t k = 0; k < c.src->kinds.size() && k < G.kinds.size(); ++k) {
      const PackCoverCell::Slot& slot = c.src->kinds[k];
      const std::uint32_t count = visibleCount(slot.count, d, radius, density);
      const PackCoverKind& kind = G.kinds[k];
      if (count > 0 && !kind.variants.empty()) {
        const auto& variant = kind.variants[std::min<std::size_t>(slot.variant, kind.variants.size() - 1)];
        const auto first = static_cast<std::uint32_t>(out.instances.size());
        out.instances.insert(out.instances.end(), all.begin() + base, all.begin() + base + count);
        for (const PackLodPart& p : near ? variant.near : variant.far) {
          PackDrawCmd dc;
          dc.geometry = p.geometry;
          dc.material = p.material;
          dc.firstInstance = first;
          dc.instanceCount = count;
          dc.frameInstances = true;
          dc.instanced = true;
          dc.tint = Tint::Color;  // instanceColor
          out.draws.push_back(dc);
        }
      }
      base += slot.count;
    }
  }
}

}  // namespace rpg::client
