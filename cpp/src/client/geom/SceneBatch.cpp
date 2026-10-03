#include "client/geom/SceneBatch.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "client/ClientWorld.h"
#include "client/Presentation.h"
#include "client/fx/FxState.h"
#include "core/data/GameData.h"
#include "core/protocol/Replicated.h"
#include "core/world/StaticWorld.h"

namespace rpg::client {

namespace proto = protocol;
using glm::mat4;
using glm::vec3;

namespace {

mat4 at(double x, double y, double z) { return glm::translate(mat4(1.0f), vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z))); }
mat4 rotY(const mat4& m, double a) { return glm::rotate(m, static_cast<float>(a), vec3(0, 1, 0)); }
mat4 scaled(const mat4& m, double sx, double sy, double sz) {
  return glm::scale(m, vec3(static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz)));
}

std::uint32_t mix(std::uint32_t a, std::uint32_t b, double t) {
  const auto ch = [&](int s) {
    const double va = (a >> s) & 255, vb = (b >> s) & 255;
    return static_cast<std::uint32_t>(std::clamp(va + (vb - va) * t, 0.0, 255.0)) << s;
  };
  return ch(16) | ch(8) | ch(0);
}

constexpr std::uint32_t kTrunk = 0x5b4431, kCanopy = 0x3d5a2e, kRock = 0x77726a, kBuilding = 0x8a7a64, kFence = 0x6e5a42;
constexpr std::uint32_t kTurbRock = 0x4a3a5e, kTurbTree = 0x3b2d4f;

std::uint32_t nodeColor(const std::string& kind) {
  if (kind == "minerio") return 0x8a6d58;
  if (kind == "madeira") return 0x8a6238;
  if (kind == "erva") return 0x8cc97a;
  return 0x8fd6ea;  // cristal
}

std::uint32_t serviceColor(const std::string& kind) {
  if (kind == "market") return 0xb24a3a;
  if (kind == "forge") return 0x5a5048;
  if (kind == "trainer") return 0x3f6f8f;
  if (kind == "storage") return 0x7a5c3e;
  if (kind == "stable") return 0x8a6a3a;
  if (kind == "board") return 0xc9a25a;
  if (kind == "canteiro") return 0xdcaa48;
  if (kind == "vigia") return 0x6b6358;
  if (kind == "observatory" || kind == "astronomer") return 0x3b4f7a;
  return 0x999080;
}

// Humanoide: corpo (cápsula), cabeça (esfera) e um marcador de frente (o nariz do grey-box).
void humanoid(InstanceLists& out, double x, double y, double z, double yaw, std::uint32_t cloth, std::uint32_t skin,
              std::uint32_t trim, bool crouch, bool down, float alpha, double scale = 1.0) {
  const double h = (crouch ? 1.35 : 1.8) * scale;
  mat4 base = rotY(at(x, y, z), yaw);
  if (down) base = glm::rotate(glm::translate(base, vec3(0, 0.35f, 0)), glm::radians(90.0f), vec3(1, 0, 0));
  out.add(Prim::Capsule, makeInstance(scaled(base, scale, h / 1.8, scale), cloth, alpha));
  out.add(Prim::Sphere, makeInstance(scaled(glm::translate(base, vec3(0, static_cast<float>(h - 0.1 * scale), 0)), 0.2 * scale, 0.22 * scale, 0.2 * scale), skin, alpha));
  out.add(Prim::Box, makeInstance(scaled(glm::translate(base, vec3(0, static_cast<float>(h * 0.72), 0.3f * static_cast<float>(scale))), 0.28 * scale, 0.12 * scale, 0.12 * scale), trim, alpha));
}

// Arma na mão direita, girando com o progresso do golpe.
void weapon(InstanceLists& out, double x, double y, double z, double yaw, double progress, std::uint32_t color, bool bow) {
  mat4 m = rotY(at(x, y, z), yaw);
  m = glm::translate(m, vec3(-0.42f, 1.15f, 0.15f));
  const double swing = progress >= 0 ? std::sin(progress * 3.14159265) * 1.6 : 0.0;
  m = glm::rotate(m, static_cast<float>(-0.4 - swing), vec3(1, 0, 0));
  if (bow) out.add(Prim::Box, makeInstance(scaled(m, 0.05, 1.3, 0.12), color));
  else out.add(Prim::Box, makeInstance(scaled(glm::translate(m, vec3(0, 0.45f, 0)), 0.07, 0.9, 0.12), color));
}

}  // namespace

InstanceLists buildStaticProps(const StaticMap& map, const GameplayLayout& layout) {
  InstanceLists out;
  const bool turb = map.kind() == MapKind::Turbulent;
  for (const Collider& c : map.collision().colliders()) {
    const double gy = map.groundHeight(c.x, c.z);
    if (c.type == Collider::Type::Circle) {
      if (c.r <= 1.0) {  // árvore
        const double trunkH = 4.5;
        out.add(Prim::Cylinder, makeInstance(scaled(at(c.x, gy - 0.2, c.z), c.r * 0.7, trunkH, c.r * 0.7), turb ? kTurbTree : kTrunk));
        out.add(Prim::Cone, makeInstance(scaled(at(c.x, gy + 2.6, c.z), 2.2, 5.5, 2.2), turb ? kTurbTree : kCanopy));
      } else {  // rocha
        out.add(Prim::Sphere, makeInstance(scaled(at(c.x, gy + c.r * 0.2, c.z), c.r, c.r * 0.75, c.r), turb ? kTurbRock : kRock));
      }
    } else {
      const double rot = -std::atan2(c.s, c.c);
      const bool thin = std::min(c.hw, c.hd) < 0.45;
      const double h = thin ? 1.3 : (std::max(c.hw, c.hd) > 6 ? 5.5 : 3.6);
      out.add(Prim::Box, makeInstance(scaled(rotY(at(c.x, gy + h * 0.5 - 0.3, c.z), rot), c.hw * 2, h, c.hd * 2),
                                      turb ? kTurbRock : thin ? kFence : kBuilding));
    }
  }
  if (!turb) {
    for (const LayoutInteractable& it : layout.interactables) {
      if (it.kind == "node") continue;  // pontos de coleta mudam (cargas): entram a cada quadro
      const double gy = map.groundHeight(it.x, it.z);
      out.add(Prim::Box, makeInstance(scaled(at(it.x, gy + 1.1, it.z), 2.2, 2.2, 1.6), serviceColor(it.kind)));
      out.add(Prim::Cylinder, makeInstance(scaled(at(it.x, gy + 2.2, it.z), 0.12, 1.6, 0.12), 0x5b4431));
      out.add(Prim::Sphere, makeInstance(scaled(at(it.x, gy + 3.9, it.z), 0.35, 0.35, 0.35), serviceColor(it.kind)));
    }
  }
  return out;
}

void buildEntityInstances(const SceneContext& ctx, const ClientWorld& world, double time, InstanceLists& out) {
  const StaticWorld& statics = *ctx.statics;
  const GameplayLayout& L = statics.layout();
  const MapKind myMap = world.map();
  const StaticMap& map = statics.map(myMap);
  const proto::WorldState* W = world.world();
  const proto::PrivateState* self = world.self();

  // pontos de coleta (esgotados ficam escuros e baixos)
  if (myMap == MapKind::Region && W && !(self && self->turb.inside) && !ctx.packModels) {
    for (const LayoutInteractable& it : L.interactables) {
      if (it.kind != "node" || it.node < 0) continue;
      const auto ni = static_cast<std::size_t>(it.node);
      const LayoutNode& n = L.nodes[ni];
      const bool full = ni >= W->nodes.size() || W->nodes[ni].charges > 0;
      const double gy = map.groundHeight(it.x, it.z);
      const std::uint32_t col = full ? nodeColor(n.kind) : mix(nodeColor(n.kind), 0x202020, 0.6);
      const double s = full ? 1.0 : 0.5;
      if (n.kind == "madeira")
        out.add(Prim::Cylinder, makeInstance(scaled(glm::rotate(rotY(at(it.x, gy + 0.4, it.z), n.rot), 1.5708f, vec3(0, 0, 1)), 0.4, 2.4 * s, 0.4), col));
      else if (n.kind == "cristal")
        out.add(Prim::Cone, makeInstance(scaled(at(it.x, gy, it.z), 0.6, 1.8 * s, 0.6), col));
      else if (n.kind == "erva")
        out.add(Prim::Sphere, makeInstance(scaled(at(it.x, gy + 0.2, it.z), 0.7, 0.45 * s, 0.7), col));
      else
        out.add(Prim::Sphere, makeInstance(scaled(at(it.x, gy + 0.3, it.z), 1.1, 0.8 * s, 1.1), col));
    }
  }
  // santuários e saídas da incursão
  if (self && self->turb.inside && !ctx.packModels) {
    const XZ T = L.turbulent.center;
    for (std::size_t i = 0; i < L.turbulent.shrines.size(); ++i) {
      const bool taken = i < self->turb.shrinesTaken.size() && self->turb.shrinesTaken[i];
      const double x = T.x + L.turbulent.shrines[i].x, z = T.z + L.turbulent.shrines[i].z, gy = map.groundHeight(x, z);
      out.add(Prim::Cylinder, makeInstance(scaled(at(x, gy, z), 0.9, 1.2, 0.9), 0x4a3a5e));
      if (!taken) out.add(Prim::Sphere, makeInstance(scaled(at(x, gy + 1.8 + std::sin(time * 2) * 0.15, z), 0.35, 0.35, 0.35), 0xb892ff));
    }
    for (const XZ& e : L.turbulent.exits) {
      const double x = T.x + e.x, z = T.z + e.z, gy = map.groundHeight(x, z);
      out.add(Prim::Box, makeInstance(scaled(at(x, gy + 1.5, z), 1.2, 3.0, 1.2), 0xd9d2c0));
    }
  }
  // o rasgo violeta
  if (W && W->portal && myMap == MapKind::Region && !ctx.packModels) {
    const double gy = map.groundHeight(W->portalX, W->portalZ);
    mat4 m = rotY(at(W->portalX, gy + 3, W->portalZ), time * 0.4);
    out.add(Prim::Sphere, makeInstance(scaled(m, 1.4, 3.0, 0.25), 0xb892ff, 0.85f));
  }

  for (const proto::EntityState& e : world.entities()) {
    if (static_cast<MapKind>(e.map) != myMap) continue;
    if (ctx.packCharacters && (e.kind == proto::EntityKind::Player || e.kind == proto::EntityKind::Enemy ||
                               e.kind == proto::EntityKind::Npc || e.kind == proto::EntityKind::Mount))
      continue;
    const bool down = (e.flags & proto::kFlagDown) != 0;
    switch (e.kind) {
      case proto::EntityKind::Player: {
        if (e.flags & proto::kFlagBlink) break;
        const OriginLook& o = ctx.look->origin(e.origin);
        const bool anon = (e.flags & proto::kFlagAnon) && e.id != world.playerId();
        const bool mounted = (e.flags & proto::kFlagMounted) != 0;
        const double y = e.y + (mounted ? 1.05 : 0.0);
        const std::uint32_t cloth = anon ? 0x3a2f4a : o.cloth;
        humanoid(out, e.x, y, e.z, e.yaw, cloth, anon ? 0x5a4a6a : o.skin, anon ? 0x6b4fa0 : o.trim,
                 (e.flags & proto::kFlagCrouch) != 0, down, 1.0f);
        if (!down && e.weapon != 0xFFFF && ctx.data->weapons.contains(WeaponFamilyId{e.weapon})) {
          const bool bow = ctx.data->weapons.key(WeaponFamilyId{e.weapon}) == "arco";
          weapon(out, e.x, y, e.z, e.yaw, e.actionProgress, 0xc4bfb3, bow);
        }
        break;
      }
      case proto::EntityKind::Enemy: {
        const EnemyTypeId type{e.type};
        const bool known = ctx.data->enemies.contains(type);
        const Faction f = known ? ctx.data->enemies[type].faction : Faction::Beast;
        const std::string& key = known ? ctx.data->enemies.key(type) : std::string{};
        const double fade = down ? std::max(0.0, 1.0 - std::max(0.0, e.deadT - 2.0) / 1.5) : 1.0;
        if (fade <= 0.02) break;
        const double pulse = e.hurtT > 0 ? std::min(1.0, e.hurtT * 5) : 0.0;
        if (e.flags & proto::kFlagHuman) {
          std::uint32_t cloth = f == Faction::Guard ? 0x3a5f8a : f == Faction::Shadow ? 0x4b3a6b : 0x7a3b2e;
          if (key.find("Arco") != std::string::npos) cloth = 0x6a4a2a;
          cloth = mix(cloth, 0xffffff, pulse * 0.6);
          humanoid(out, e.x, e.y, e.z, e.yaw, cloth, f == Faction::Shadow ? 0x2a2038 : 0xb08a6a, 0x3a3028, false, down,
                   static_cast<float>(f == Faction::Shadow ? 0.8 * fade : fade));
          if (!down && e.weapon != 0xFFFF && ctx.data->weapons.contains(WeaponFamilyId{e.weapon})) {
            const bool bow = ctx.data->weapons.key(WeaponFamilyId{e.weapon}) == "arco";
            weapon(out, e.x, e.y, e.z, e.yaw, e.actionProgress, 0x8a8478, bow);
          }
        } else {
          const double s = e.scale;
          std::uint32_t col = key.find("Palida") != std::string::npos ? 0xb7ad9c : key.find("Lider") != std::string::npos ? 0x4e3f33 : 0x6b5846;
          if (e.state == static_cast<std::uint8_t>(proto::EnemyState::Windup)) col = mix(col, 0xff5a3c, 0.35);
          col = mix(col, 0xffffff, pulse * 0.6);
          mat4 base = rotY(at(e.x, e.y, e.z), e.yaw);
          if (down) base = glm::rotate(base, glm::radians(80.0f), vec3(0, 0, 1));
          const float a = static_cast<float>(fade);
          out.add(Prim::Box, makeInstance(scaled(glm::translate(base, vec3(0, static_cast<float>(0.75 * s), 0)), 0.7 * s, 0.75 * s, 1.5 * s), col, a));
          out.add(Prim::Sphere, makeInstance(scaled(glm::translate(base, vec3(0, static_cast<float>(1.05 * s), static_cast<float>(0.9 * s))), 0.32 * s, 0.3 * s, 0.4 * s), col, a));
          for (int leg = 0; leg < 4; ++leg) {
            const float lx = (leg & 1 ? 0.25f : -0.25f) * static_cast<float>(s), lz = (leg & 2 ? 0.5f : -0.5f) * static_cast<float>(s);
            const double phase = e.speed > 0.3 ? std::sin(time * 9 + leg * 1.7) * 0.15 : 0.0;
            out.add(Prim::Box, makeInstance(scaled(glm::translate(base, vec3(lx, static_cast<float>(0.2 * s), lz + static_cast<float>(phase * s))), 0.16 * s, 0.45 * s, 0.16 * s), mix(col, 0x000000, 0.2), a));
          }
        }
        break;
      }
      case proto::EntityKind::Npc: {
        std::uint32_t cloth = 0x8a7a5a, skin = 0xc99a78;
        if (e.type < L.npcs.size()) {
          const LayoutNpc& n = L.npcs[e.type];
          if (!n.cloth.empty()) cloth = parseHexColor(n.cloth);
          skin = ctx.look->origin(n.race).skin;
        }
        humanoid(out, e.x, e.y, e.z, e.yaw, cloth, skin, 0xc9a25a, false, false, 1.0f);
        break;
      }
      case proto::EntityKind::Mount: {
        mat4 base = rotY(at(e.x, e.y, e.z), e.yaw);
        const std::uint32_t col = 0x7a5a3c;
        out.add(Prim::Box, makeInstance(scaled(glm::translate(base, vec3(0, 1.25f, 0)), 0.75, 0.8, 2.0), col));
        out.add(Prim::Box, makeInstance(scaled(glm::rotate(glm::translate(base, vec3(0, 1.75f, 1.05f)), -0.6f, vec3(1, 0, 0)), 0.35, 0.9, 0.4), col));
        for (int leg = 0; leg < 4; ++leg) {
          const float lx = leg & 1 ? 0.25f : -0.25f, lz = leg & 2 ? 0.75f : -0.75f;
          out.add(Prim::Box, makeInstance(scaled(glm::translate(base, vec3(lx, 0.45f, lz)), 0.16, 0.9, 0.16), 0x5a4030));
        }
        break;
      }
      case proto::EntityKind::Projectile: {
        if (ctx.packModels) break;
        const auto kind = proto::projectileKind(e.type);
        if (kind == proto::ProjectileKind::Spell) {
          out.add(Prim::Sphere, makeInstance(scaled(at(e.x, e.y, e.z), 0.22, 0.22, 0.22), 0xb892ff));
        } else {
          const glm::dvec3 d = glm::length(glm::dvec3(e.dx, e.dy, e.dz)) > 1e-6 ? glm::normalize(glm::dvec3(e.dx, e.dy, e.dz)) : glm::dvec3(0, 0, 1);
          const double yaw = std::atan2(d.x, d.z), pitch = std::asin(std::clamp(d.y, -1.0, 1.0));
          mat4 m = glm::rotate(rotY(at(e.x, e.y, e.z), yaw), static_cast<float>(-pitch), vec3(1, 0, 0));
          out.add(Prim::Box, makeInstance(scaled(m, 0.05, 0.05, kind == proto::ProjectileKind::Bolt ? 0.5 : 0.8), 0xd9c9a0));
        }
        break;
      }
      case proto::EntityKind::LootBag: {
        if (ctx.packModels) break;
        const bool own = (e.flags & proto::kFlagOwn) != 0;
        out.add(Prim::Box, makeInstance(scaled(at(e.x, e.y + 0.25, e.z), 0.6, 0.5, 0.6), own ? 0xc9a25a : 0x6e5438));
        break;
      }
    }
  }
}

void buildFxGeometry(const FxState& fx, const StaticMap& map, std::uint8_t mapKind, const HoverRing& ring, double time,
                     std::vector<ColorVertex>& out) {
  const auto color = [](std::uint32_t rgb, double a) {
    return packHex(rgb, static_cast<std::uint8_t>(std::clamp(a, 0.0, 1.0) * 255.0 + 0.5));
  };
  const auto tri = [&](vec3 a, vec3 b, vec3 c, std::uint32_t col) {
    out.push_back({a.x, a.y, a.z, col});
    out.push_back({b.x, b.y, b.z, col});
    out.push_back({c.x, c.y, c.z, col});
  };
  // telegrafias (fx.js `telegraph`): contorno fraco + preenchimento que cresce até o golpe
  for (const TelegraphFx& t : fx.telegraphs) {
    const proto::EvTelegraph& o = t.ev;
    if (o.map != mapKind) continue;
    const double k = o.duration > 0 ? std::min(1.0, t.t / o.duration) : 1.0;
    const double cs = std::cos(o.facing), sn = std::sin(o.facing);
    const auto world = [&](double lx, double lz, double lift) {
      const double wx = o.x + lx * cs + lz * sn, wz = o.z - lx * sn + lz * cs;
      return vec3(static_cast<float>(wx), static_cast<float>(map.groundHeight(wx, wz) + lift), static_cast<float>(wz));
    };
    std::vector<std::pair<double, double>> pts;
    if (o.shape == proto::TeleShape::Line) {
      const double w = o.width / 2;
      pts = {{-w, 0}, {w, 0}, {w, o.length}, {-w, o.length}};
    } else {
      pts.push_back({0, 0});
      const double arc = o.shape == proto::TeleShape::Circle ? 6.283185307179586 : o.arc;
      const int seg = o.shape == proto::TeleShape::Circle ? 28 : 14;
      for (int i = 0; i <= seg; ++i) {
        const double a = -arc / 2 + arc * i / seg;
        pts.push_back({std::sin(a) * o.radius, std::cos(a) * o.radius});
      }
    }
    const bool line = o.shape == proto::TeleShape::Line;
    const auto fan = [&](double sx, double sz, double lift, std::uint32_t col) {
      for (std::size_t i = 1; i + 1 < pts.size(); ++i)
        tri(world(pts[0].first * sx, pts[0].second * sz, lift), world(pts[i].first * sx, pts[i].second * sz, lift),
            world(pts[i + 1].first * sx, pts[i + 1].second * sz, lift), col);
    };
    fan(1, 1, 0.12, color(o.color, 0.18));
    const double f = std::max(0.01, k);
    fan(line ? 1 : f, f, 0.14, color(o.color, k >= 1 ? 0.65 : 0.42));
  }
  // anel de seleção (fx.js `setHoverRing`)
  if (ring.visible) {
    const double gy = map.groundHeight(ring.x, ring.z) + 0.14, rot = time * 1.5;
    const std::uint32_t col = color(ring.color, 0.75);
    constexpr int kSeg = 28;
    for (int i = 0; i < kSeg; ++i) {
      const double a0 = rot + 6.283185307179586 * i / kSeg, a1 = rot + 6.283185307179586 * (i + 1) / kSeg;
      const auto p = [&](double a, double r) {
        return vec3(static_cast<float>(ring.x + std::sin(a) * r), static_cast<float>(gy), static_cast<float>(ring.z + std::cos(a) * r));
      };
      tri(p(a0, ring.r * 0.72), p(a0, ring.r), p(a1, ring.r), col);
      tri(p(a0, ring.r * 0.72), p(a1, ring.r), p(a1, ring.r * 0.72), col);
    }
  }
  // partículas (cubos de 0,14 m que encolhem com a vida)
  for (const ParticleFx& p : fx.particles) {
    const float s = static_cast<float>(0.07 * std::max(0.05, p.life * 1.6));
    const vec3 c(static_cast<float>(p.pos.x), static_cast<float>(p.pos.y), static_cast<float>(p.pos.z));
    const std::uint32_t col = color(p.color, 1.0);
    const vec3 v[8] = {c + vec3(-s, -s, -s), c + vec3(s, -s, -s), c + vec3(s, s, -s), c + vec3(-s, s, -s),
                       c + vec3(-s, -s, s),  c + vec3(s, -s, s),  c + vec3(s, s, s),  c + vec3(-s, s, s)};
    constexpr int f[6][4] = {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7}, {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0}};
    for (const auto& q : f) {
      tri(v[q[0]], v[q[1]], v[q[2]], col);
      tri(v[q[0]], v[q[2]], v[q[3]], col);
    }
  }
}

}  // namespace rpg::client
