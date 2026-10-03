#include "client/world/EntityModels.h"

#include <array>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "client/ClientWorld.h"
#include "core/protocol/Replicated.h"
#include "core/world/StaticWorld.h"

namespace rpg::client {

namespace proto = protocol;

std::string projectileModelKey(std::uint16_t type) {
  static constexpr std::array<const char*, 8> kSchools = {"", "ice", "fire", "nature", "wind", "curse", "vital", "holy"};
  const auto kind = proto::projectileKind(type);
  const std::uint8_t school = proto::projectileSchool(type);
  if (kind == proto::ProjectileKind::Spell || school != 0)
    return school != 0 && school < kSchools.size() ? std::string("spell:") + kSchools[school] : std::string("spell");
  if (kind == proto::ProjectileKind::Bolt) return "bolt";
  switch (proto::projectileOwner(type)) {
    case proto::ProjectileOwner::Player: return "arrow:player";
    case proto::ProjectileOwner::Shadow: return "arrow:shadow";
    default: return "arrow:enemy";
  }
}

glm::mat4 rotationFromZ(glm::vec3 dir) {
  const float len = glm::length(dir);
  if (len < 1e-6f) return glm::mat4(1.0f);
  const glm::vec3 to = dir / len, from(0, 0, 1);
  const float r = glm::dot(from, to) + 1.0f;
  glm::quat q;
  if (r < 1e-6f) {
    q = glm::quat(0.0f, 0.0f, 1.0f, 0.0f);  // 180° em torno de Y (eixo ortogonal qualquer)
  } else {
    const glm::vec3 c = glm::cross(from, to);
    q = glm::normalize(glm::quat(r, c.x, c.y, c.z));
  }
  return glm::mat4_cast(q);
}

void collectEntityModels(const ScenePack& pack, const ClientWorld& world, const StaticWorld& statics, std::vector<ModelInstance>& out) {
  const MapKind myMap = world.map();
  const StaticMap& map = statics.map(myMap);
  const proto::WorldState* W = world.world();
  const auto tpl = [&](const char* name) -> const std::vector<PackDraw>* {
    const auto it = pack.templates.find(name);
    return it == pack.templates.end() ? nullptr : &it->second;
  };
  if (W && W->portal && myMap == MapKind::Region) {
    ModelInstance m;
    m.parts = tpl("portal");
    m.root = glm::translate(glm::mat4(1.0f), glm::vec3(static_cast<float>(W->portalX), static_cast<float>(map.groundHeight(W->portalX, W->portalZ)),
                                                       static_cast<float>(W->portalZ)));
    m.portal = true;
    out.push_back(m);
  }
  for (const proto::EntityState& e : world.entities()) {
    if (static_cast<MapKind>(e.map) != myMap) continue;
    if (e.kind == proto::EntityKind::LootBag) {
      ModelInstance m;
      m.parts = tpl((e.flags & proto::kFlagOwn) ? "lootBagOwn" : "lootBag");
      m.root = glm::translate(glm::mat4(1.0f), glm::vec3(static_cast<float>(e.x), static_cast<float>(map.groundHeight(e.x, e.z)), static_cast<float>(e.z)));
      out.push_back(m);
    } else if (e.kind == proto::EntityKind::Projectile) {
      const auto it = pack.projectiles.find(projectileModelKey(e.type));
      if (it == pack.projectiles.end()) continue;
      ModelInstance m;
      m.parts = &it->second;
      m.root = glm::translate(glm::mat4(1.0f), glm::vec3(static_cast<float>(e.x), static_cast<float>(e.y), static_cast<float>(e.z))) *
               rotationFromZ(glm::vec3(static_cast<float>(e.dx), static_cast<float>(e.dy), static_cast<float>(e.dz)));
      out.push_back(m);
    }
  }
}

}  // namespace rpg::client
