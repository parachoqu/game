#include "core/world/CollisionGrid.h"

#include <algorithm>
#include <cmath>

#include "core/Math.h"

namespace rpg {

void CollisionGrid::addCircle(double x, double z, double r) {
  Collider c;
  c.type = Collider::Type::Circle;
  c.x = x;
  c.z = z;
  c.r = r;
  add(c);
}

void CollisionGrid::addBox(double x, double z, double hw, double hd, double rotY) {
  const double rot = -rotY;
  Collider c;
  c.type = Collider::Type::Box;
  c.x = x;
  c.z = z;
  c.hw = hw;
  c.hd = hd;
  c.c = std::cos(rot);
  c.s = std::sin(rot);
  c.r = jsHypot(hw, hd);
  add(c);
}

// collide.js `insert`: o colisor entra em todas as células que o círculo envolvente toca.
void CollisionGrid::add(const Collider& c) {
  const auto index = static_cast<std::uint32_t>(colliders_.size());
  colliders_.push_back(c);
  const double fi0 = std::floor((c.x - c.r) / cell_), fi1 = std::floor((c.x + c.r) / cell_);
  const double fj0 = std::floor((c.z - c.r) / cell_), fj1 = std::floor((c.z + c.r) / cell_);
  // Na demo, os nós de erva-lume pedem "sem colisor" com { r: 0 }, que cai numa caixa de tamanho NaN
  // (world.js `place`). No JS o laço `for (i = NaN; i <= NaN; …)` não roda e o colisor nunca entra na
  // grade: fica registrado mas não bloqueia nada. Aqui o efeito é o mesmo, sem converter NaN em inteiro.
  if (!std::isfinite(fi0) || !std::isfinite(fi1) || !std::isfinite(fj0) || !std::isfinite(fj1)) return;
  const auto i0 = static_cast<std::int64_t>(fi0), i1 = static_cast<std::int64_t>(fi1);
  const auto j0 = static_cast<std::int64_t>(fj0), j1 = static_cast<std::int64_t>(fj1);
  for (std::int64_t i = i0; i <= i1; ++i)
    for (std::int64_t j = j0; j <= j1; ++j) cells_[key(i, j)].push_back(index);
}

const std::vector<std::uint32_t>* CollisionGrid::cellAt(double x, double z) const {
  const auto it = cells_.find(key(static_cast<std::int64_t>(std::floor(x / cell_)), static_cast<std::int64_t>(std::floor(z / cell_))));
  return it == cells_.end() ? nullptr : &it->second;
}

// collide.js `resolve`, linha a linha (Math.hypot do V8 em jsHypot).
XZ CollisionGrid::resolve(double x, double z, double r) const {
  const auto* list = cellAt(x, z);
  if (!list) return {x, z};
  for (const std::uint32_t idx : *list) {
    const Collider& o = colliders_[idx];
    if (o.type == Collider::Type::Circle) {
      const double dx = x - o.x, dz = z - o.z, d = jsHypot(dx, dz), m = o.r + r;
      if (d < m && d > 1e-4) {
        x = o.x + dx / d * m;
        z = o.z + dz / d * m;
      }
    } else {
      // para o espaço local da caixa
      const double dx = x - o.x, dz = z - o.z;
      const double lx = dx * o.c + dz * o.s, lz = -dx * o.s + dz * o.c;
      const double cx = std::max(-o.hw, std::min(o.hw, lx)), cz = std::max(-o.hd, std::min(o.hd, lz));
      const double px = lx - cx, pz = lz - cz, d = jsHypot(px, pz);
      if (d < r) {
        double nlx, nlz;
        if (d > 1e-4) {
          nlx = cx + px / d * r;
          nlz = cz + pz / d * r;
        } else {  // centro dentro da caixa: sai pelo lado mais próximo
          const double ox = o.hw - std::fabs(lx), oz = o.hd - std::fabs(lz);
          if (ox < oz) {
            nlx = signOr1(lx) * (o.hw + r);
            nlz = lz;
          } else {
            nlx = lx;
            nlz = signOr1(lz) * (o.hd + r);
          }
        }
        x = o.x + nlx * o.c - nlz * o.s;
        z = o.z + nlx * o.s + nlz * o.c;
      }
    }
  }
  return {x, z};
}

bool CollisionGrid::testPoint(double x, double z, double r) const {
  const auto* list = cellAt(x, z);
  if (!list) return false;
  for (const std::uint32_t idx : *list) {
    const Collider& o = colliders_[idx];
    if (o.type == Collider::Type::Circle) {
      if (jsHypot(x - o.x, z - o.z) < o.r + r) return true;
    } else {
      const double dx = x - o.x, dz = z - o.z;
      const double lx = dx * o.c + dz * o.s, lz = -dx * o.s + dz * o.c;
      if (std::fabs(lx) < o.hw + r && std::fabs(lz) < o.hd + r) return true;
    }
  }
  return false;
}

}  // namespace rpg
