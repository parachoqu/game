#include "client/entities/CharacterViews.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include <glm/gtc/matrix_transform.hpp>

#include "client/ClientWorld.h"
#include "client/game/Lighting.h"
#include "core/Math.h"
#include "core/data/GameData.h"
#include "core/protocol/Replicated.h"
#include "core/world/StaticWorld.h"

namespace rpg::client {

namespace proto = rpg::protocol;
using anim::HumanoidAnimator;
using anim::HumanoidInput;
using anim::QuadrupedAnimator;
using anim::QuadrupedInput;

namespace {

constexpr std::uint16_t kNoWeapon = 0xFFFF;
constexpr double NPC_NEAR = 70;          // npcs.js: só anima e mostra perto do jogador
constexpr double ENEMY_ASLEEP = 110;     // enemies.js: parado e longe some

glm::vec3 linearOf(std::uint32_t rgb) { return srgbHexToLinear(rgb); }

// characters.js `variant`: o clone ganha o recurso `occ` (pontilhado entre a câmera e o personagem)
PackMaterial tinted(const PackMaterial& b, std::optional<std::uint32_t> tint, std::optional<std::uint32_t> emissive, float emissiveIntensity) {
  PackMaterial m = b;
  if (tint) m.color = linearOf(*tint);
  if (emissive) {
    m.emissive = linearOf(*emissive);
    m.emissiveIntensity = emissiveIntensity;
  }
  m.features |= kFeatOcc;
  return m;
}

// characters.js `shadowed`: silhueta escura com brilho violeta, só com o relevo da textura normal
PackMaterial shadowed(const PackMaterial& b) {
  PackMaterial m;
  m.name = b.name + ":sombra";
  m.type = "MeshStandardMaterial";
  m.features = kFeatOcc;
  m.color = linearOf(0x2a2238);
  m.emissive = linearOf(0x5a34a0);
  m.emissiveIntensity = 0.8f;
  m.roughness = 0.5f;
  m.metalness = 0.1f;
  m.normalMap = b.normalMap;
  return m;
}

glm::mat4 rootMatrix(double x, double y, double z, double yaw, double scale) {
  glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)));
  m = glm::rotate(m, static_cast<float>(yaw), glm::vec3(0, 1, 0));
  if (scale != 1.0) m = glm::scale(m, glm::vec3(static_cast<float>(scale)));
  return m;
}

}  // namespace

// ---------------------------------------------------------------- biblioteca

CharacterLibrary::CharacterLibrary(ScenePack& pack) : meta_(pack.clipMeta) {
  for (const auto& [id, pr] : pack.rigs) rigs_.emplace(id, anim::Rig::fromPack(pack, id));
  const auto add = [&](PackMaterial m) {
    pack.materials.push_back(std::move(m));
    return static_cast<int>(pack.materials.size()) - 1;
  };
  for (const auto& [id, rig] : rigs_) {
    std::vector<int> plain;
    for (const anim::RigMesh& m : rig.meshes) plain.push_back(m.material);
    materials_[{id, CharacterLook::Plain}] = plain;
    // cada aparência é um material a mais por material do molde (o cache de `variant`)
    const auto variant = [&](CharacterLook look, const std::function<PackMaterial(const PackMaterial&)>& make) {
      std::map<int, int> made;
      std::vector<int> out;
      for (int base : plain) {
        if (base < 0) {
          out.push_back(base);
          continue;
        }
        auto it = made.find(base);
        if (it == made.end()) it = made.emplace(base, add(make(pack.materials[static_cast<std::size_t>(base)]))).first;
        out.push_back(it->second);
      }
      materials_[{id, look}] = std::move(out);
    };
    if (rig.kind == "humanoid") {
      variant(CharacterLook::Guard, [](const PackMaterial& b) { return tinted(b, 0xb9c8e6, std::nullopt, 1); });
      variant(CharacterLook::Bandit, [](const PackMaterial& b) { return tinted(b, 0xd2ae9c, std::nullopt, 1); });
      variant(CharacterLook::Shadow, shadowed);
    } else if (rig.kind == "hound") {
      variant(CharacterLook::HoundLeader, [](const PackMaterial& b) { return tinted(b, std::nullopt, 0x4a2470, 0.6f); });
      variant(CharacterLook::HoundPale, [](const PackMaterial& b) { return tinted(b, 0xc9c3d6, 0x2a2632, 0.6f); });
    } else if (id == "lioness") {
      variant(CharacterLook::LionessMane, [](const PackMaterial& b) { return tinted(b, 0xb08a6c, std::nullopt, 1); });
      variant(CharacterLook::LionessPale, [](const PackMaterial& b) { return tinted(b, 0xe6e0d6, 0x2e2a26, 1); });
    }
  }
}

const anim::Rig* CharacterLibrary::rig(std::string_view id) const {
  const auto it = rigs_.find(id);
  return it == rigs_.end() ? nullptr : &it->second;
}

const std::vector<int>& CharacterLibrary::materials(std::string_view rig, CharacterLook look) const {
  if (const auto it = materials_.find(std::make_pair(std::string(rig), look)); it != materials_.end()) return it->second;
  static const std::vector<int> kNone;
  const auto plain = materials_.find(std::make_pair(std::string(rig), CharacterLook::Plain));
  return plain == materials_.end() ? kNone : plain->second;
}

glm::vec2 raceScale(std::string_view origin) {
  if (origin == "elfo") return {0.95f, 1.04f};
  if (origin == "anao") return {1.12f, 0.78f};
  if (origin == "orc") return {1.08f, 1.06f};
  return {1.0f, 1.0f};
}

bool weaponInLeftHand(std::string_view f) {
  return f == "arco" || f == "tomo_fogo" || f == "tomo_vento" || f == "tomo_maldicao" || f == "tomo_sagrado";
}
bool weaponInBothHands(std::string_view f) { return f == "manoplas" || f == "adaga"; }

// ---------------------------------------------------------------- vistas

struct CharacterViews::View {
  std::string identity;  // tipo, molde, aparência e escala: mudou, a vista é refeita
  const anim::Rig* rig = nullptr;
  const std::vector<int>* materials = nullptr;
  std::unique_ptr<HumanoidAnimator> human;
  std::unique_ptr<QuadrupedAnimator> quad;
  std::string family, weaponKey;  // família na mão e a chave do modelo em ScenePack::weapons
  bool glow = false;
  std::uint32_t seen = 0;
  // jogador: esquiva direcional (player.js) — o corpo gira para o clipe mostrar o sentido do passo
  bool dodging = false;
  double yawBefore = 0, visYaw = 0, visHold = 0;
  std::optional<double> dodgeVis;
  std::string dodgeKey;
  bool visInit = false;
  double aimT = 0;  // player.js `updateCamera`: P.aimT → aiming com min(1, dt·9)
  std::vector<glm::mat4> nodes;
};

CharacterViews::CharacterViews(const ScenePack& pack, const CharacterLibrary& lib) : pack_(pack), lib_(lib) {}
CharacterViews::~CharacterViews() = default;

const std::vector<glm::mat4>* CharacterViews::nodeMatrices(std::uint32_t id) const {
  const auto it = views_.find(id);
  return it == views_.end() ? nullptr : &it->second->nodes;
}

const anim::Pose* CharacterViews::pose(std::uint32_t id) const {
  const auto it = views_.find(id);
  if (it == views_.end()) return nullptr;
  if (it->second->human) return &it->second->human->pose();
  if (it->second->quad) return &it->second->quad->pose();
  return nullptr;
}

CharacterViews::View* CharacterViews::viewFor(const proto::EntityState& e, const CharacterContext& ctx) {
  const GameData& data = *ctx.data;
  std::string rigId, family;
  CharacterLook look = CharacterLook::Plain;
  glm::vec2 race(1.0f);
  double scale = 1;
  enum class Animator { Human, Beast, Hound, Mount } animator = Animator::Human;
  bool glow = false;
  const auto familyOf = [&](std::uint16_t w) {
    return w != kNoWeapon && data.weapons.contains(WeaponFamilyId{w}) ? data.weapons.key(WeaponFamilyId{w}) : std::string();
  };
  switch (e.kind) {
    case proto::EntityKind::Player: {
      rigId = !e.model.empty() && lib_.rig(e.model) ? e.model : "paladina";
      race = raceScale(e.origin);
      family = familyOf(e.weapon);
      break;
    }
    case proto::EntityKind::Npc: {
      const auto& npcs = ctx.statics->layout().npcs;
      if (e.type < npcs.size()) {
        const LayoutNpc& n = npcs[e.type];
        rigId = n.model.empty() ? "kachujin" : n.model;
        race = raceScale(n.race);
        family = n.weapon;
      } else {
        rigId = "kachujin";
      }
      break;
    }
    case proto::EntityKind::Enemy: {
      const EnemyTypeId type{e.type};
      if (!data.enemies.contains(type)) return nullptr;
      const EnemyDef& cfg = data.enemies[type];
      const std::string& key = data.enemies.key(type);
      if (cfg.faction == Faction::Beast) {
        const bool mane = key == "garraLider", pale = key == "garraPalida";
        scale = cfg.scale * (mane ? 1.12 : 1.0);
        // makeBeast: com o modelo do cão do vazio, as Garras são cães; sem ele, a leoa
        if (lib_.rig("hound")) {
          rigId = "hound";
          animator = Animator::Hound;
          look = mane ? CharacterLook::HoundLeader : pale ? CharacterLook::HoundPale : CharacterLook::Plain;
        } else {
          rigId = "lioness";
          animator = Animator::Beast;
          look = mane ? CharacterLook::LionessMane : pale ? CharacterLook::LionessPale : CharacterLook::Plain;
        }
      } else {
        // enemies.js `buildModel`: sombra em silhueta, guarda azulado, saqueador avermelhado
        if (cfg.faction == Faction::Shadow) {
          rigId = "kachujin";
          look = CharacterLook::Shadow;
          glow = true;
        } else if (cfg.faction == Faction::Guard) {
          rigId = "kachujin";
          look = CharacterLook::Guard;
        } else {
          rigId = "eve";
          look = CharacterLook::Bandit;
        }
        family = familyOf(e.weapon);
        if (family.empty()) family = "espada";
      }
      break;
    }
    case proto::EntityKind::Mount: {
      rigId = "horse";
      animator = Animator::Mount;
      break;
    }
    default: return nullptr;
  }
  const anim::Rig* rig = lib_.rig(rigId);
  if (!rig) return nullptr;
  const std::string identity = std::to_string(static_cast<int>(e.kind)) + "|" + rigId + "|" + std::to_string(static_cast<int>(look)) + "|" +
                               std::to_string(race.x) + "," + std::to_string(race.y) + "|" + std::to_string(scale);
  std::unique_ptr<View>& slot = views_[e.id];
  if (!slot || slot->identity != identity) {
    slot = std::make_unique<View>();
    View& v = *slot;
    v.identity = identity;
    v.rig = rig;
    v.materials = &lib_.materials(rigId, look);
    v.glow = glow;
    if (animator == Animator::Human)
      v.human = std::make_unique<HumanoidAnimator>(*rig, lib_.clipMeta(), race, scale, e.id);
    else
      v.quad = std::make_unique<QuadrupedAnimator>(
          *rig, animator == Animator::Hound ? QuadrupedAnimator::Kind::Hound : animator == Animator::Mount ? QuadrupedAnimator::Kind::Mount : QuadrupedAnimator::Kind::Beast,
          scale, e.id);
  }
  View& v = *slot;
  if (v.human && v.family != family) {
    v.family = family;
    v.human->setWeapon(family);
    v.weaponKey = family.empty() || family == "punhos" ? std::string() : family + (v.glow ? ":sombra" : "");
  }
  return &v;
}

void CharacterViews::emit(View& v, const glm::mat4& root, const glm::vec3& center, float radius, DynamicSceneState& out) {
  const anim::Rig& rig = *v.rig;
  const anim::Pose& pose = v.human ? v.human->pose() : v.quad->pose();
  const glm::mat4 body = v.human ? v.human->bodyMatrix() : v.quad->bodyMatrix();
  anim::worldMatrices(rig, pose, root * body, v.nodes);
  for (std::size_t k = 0; k < rig.meshes.size(); ++k) {
    const anim::RigMesh& m = rig.meshes[k];
    const int material = k < v.materials->size() ? (*v.materials)[k] : m.material;
    if (m.geometry < 0 || material < 0) continue;
    const glm::mat4& meshWorld = v.nodes[static_cast<std::size_t>(m.node)];
    if (m.skinned) {
      // three (bindMode "attached"): osso_mundo · inversa · bindMatrix
      SkinnedInstance si;
      si.geometry = m.geometry;
      si.material = material;
      si.mesh = meshWorld;
      si.boneBase = static_cast<std::uint32_t>(out.bones.size());
      si.center = center;
      si.radius = radius;
      for (std::size_t b = 0; b < m.bones.size(); ++b)
        out.bones.push_back(v.nodes[static_cast<std::size_t>(m.bones[b])] * m.inverses[b] * m.bind);
      out.skinned.push_back(si);
    } else {
      ModelInstance mi;
      mi.geometry = m.geometry;
      mi.material = material;
      mi.root = meshWorld;
      out.models.push_back(mi);
    }
  }
  // arma no suporte da mão (characters.js `setWeapon`)
  if (!v.weaponKey.empty()) {
    const auto it = pack_.weapons.find(v.weaponKey);
    if (it != pack_.weapons.end()) {
      const bool left = weaponInLeftHand(v.family);
      const auto attach = [&](const std::optional<anim::Grip>& g) {
        if (!g) return;
        ModelInstance mi;
        mi.parts = &it->second;
        mi.root = v.nodes[static_cast<std::size_t>(g->node)] * g->local;
        out.models.push_back(mi);
      };
      attach(left ? rig.gripL : rig.gripR);
      if (weaponInBothHands(v.family)) attach(rig.gripL);
    }
  }
}

void CharacterViews::update(const ClientWorld& world, const CharacterContext& ctx, DynamicSceneState& out) {
  ++frame_;
  const proto::EntityState* me = world.me();
  const MapKind mapKind = world.map();
  const StaticMap& map = ctx.statics->map(mapKind);
  const auto gh = [&map](double x, double z) { return map.groundHeight(x, z); };
  const double dt = ctx.dt;
  const anim::Rig* horse = lib_.rig("horse");

  for (const proto::EntityState& e : world.entities()) {
    if (static_cast<MapKind>(e.map) != mapKind) continue;
    if (e.kind != proto::EntityKind::Player && e.kind != proto::EntityKind::Enemy && e.kind != proto::EntityKind::Npc &&
        e.kind != proto::EntityKind::Mount)
      continue;
    const double dMe = me ? std::hypot(e.x - me->x, e.z - me->z) : 0.0;
    const bool alive = (e.flags & proto::kFlagAlive) != 0;
    // npcs.js: longe do jogador o NPC nem anima; enemies.js: parado e a mais de 110 m some
    if (e.kind == proto::EntityKind::Npc && dMe >= NPC_NEAR) continue;
    if (e.kind == proto::EntityKind::Enemy && alive && dMe > ENEMY_ASLEEP && e.state == static_cast<std::uint8_t>(proto::EnemyState::Idle)) continue;
    View* vp = viewFor(e, ctx);
    if (!vp) continue;
    View& v = *vp;
    v.seen = frame_;

    anim::AnimWorld aw;
    aw.camera = ctx.camera;
    aw.groundHeight = gh;
    double rx = e.x, ry = e.y, rz = e.z, yaw = e.yaw, rootScale = 1;
    bool visible = true;

    if (v.human) {
      HumanoidInput s;
      s.dt = dt;
      if (e.kind == proto::EntityKind::Player) {
        const bool dodging = e.dodgeProgress >= 0;
        // player.js: a esquiva escolhe o clipe uma vez, pelo passo em relação a para onde o corpo olhava
        if (dodging && !v.dodging) {
          const rpg::anim::LocalDir d = rpg::anim::toLocal(v.yawBefore, std::sin(e.dodgeYaw), std::cos(e.dodgeYaw));
          const auto has = [&](rpg::anim::Clip c) { return v.human->hasClip(rpg::anim::clipKey(c)); };
          // desvio da demo: lá arco, besta e mãos vazias usam as esquivas em pé do Mixamo (standingDodgeFits),
          // e o corpo andando 4,8 m num passo agachado parece deslizar; aqui toda arma rola
          const auto c = rpg::anim::dodgeClip(d.fwd, d.left, has, false);
          v.dodgeKey = c ? std::string(rpg::anim::clipKey(*c)) : std::string();
          v.dodgeVis = e.dodgeYaw - (c ? rpg::anim::dodgeFacing(*c) : 0.0);
        }
        v.dodging = dodging;
        if (!dodging) v.yawBefore = e.yaw;
        v.visHold = dodging ? 0.25 : std::max(0.0, v.visHold - dt);
        const double target = dodging && v.dodgeVis ? *v.dodgeVis : e.yaw;
        if (!v.visInit) {
          v.visYaw = e.yaw;
          v.visInit = true;
        }
        v.visYaw = v.visHold > 0 ? lerpAngle(v.visYaw, target, dt * 22) : e.yaw;
        yaw = v.visYaw;
        const bool mounted = (e.flags & proto::kFlagMounted) != 0;
        if (mounted && horse) ry += horse->seatY - v.human->hipsY();  // quadril do cavaleiro sobre a sela
        const bool moving = (e.flags & proto::kFlagMoving) != 0;
        s.mps = mounted ? 0 : e.speed;
        s.attack = e.actionProgress;
        s.kind = static_cast<rpg::anim::ActionKind>(e.action);
        s.mounted = mounted;
        s.dodge = dodging ? e.dodgeProgress : -1;
        if (dodging) s.dodgeKey = v.dodgeKey;
        // aim: P.aimT (updateCamera). O próprio jogador usa a rampa da câmera; os outros, a flag replicada.
        if (ctx.localAim && me && e.id == me->id) {
          v.aimT = *ctx.localAim;
        } else {
          const double aiming = (e.flags & proto::kFlagAiming) != 0 ? 1.0 : 0.0;
          v.aimT += (aiming - v.aimT) * std::min(1.0, dt * 9);
        }
        s.aim = v.aimT;
        if (e.hitAge >= 0) s.hit = HumanoidInput::Hit{static_cast<rpg::anim::HitSide>(e.hitSide), e.hitAge};
        s.aimPitch = e.aimPitch;
        s.fwd = 1;
        s.strafe = 0;
        if (moving) {
          s.fwd = e.moveX * std::sin(yaw) + e.moveZ * std::cos(yaw);
          s.strafe = e.moveX * std::cos(yaw) - e.moveZ * std::sin(yaw);
          s.moveDir = glm::vec2(static_cast<float>(e.moveX), static_cast<float>(e.moveZ));
        }
        s.down = (e.flags & proto::kFlagDown) != 0;
        s.channel = (e.flags & proto::kFlagChannelAnim) != 0;
        s.metamorph = (e.flags & proto::kFlagMetamorph) != 0;
        s.crouch = (e.flags & proto::kFlagCrouch) != 0;
        if (e.airPhase >= 0) s.air = HumanoidInput::Air{e.airPhase, (e.flags & proto::kFlagAirBoosted) != 0};
        visible = (e.flags & proto::kFlagBlink) == 0;  // cintila na invulnerabilidade do reaparecimento
      } else if (e.kind == proto::EntityKind::Npc) {
        s.mps = e.speed;
        s.channel = (e.flags & proto::kFlagChannelAnim) != 0;
      } else {
        // enemies.js `enemyAnimState` (humanoides); o servidor já resolve ataque e preparo
        s.mps = e.speed;
        s.attack = e.actionProgress;
        s.kind = static_cast<rpg::anim::ActionKind>(e.action);
        if (!alive) s.death = HumanoidInput::Death{static_cast<rpg::anim::DeathFall>(e.deathFall), e.deadT};
        else if (e.hitAge >= 0) s.hit = HumanoidInput::Hit{static_cast<rpg::anim::HitSide>(e.hitSide), e.hitAge};
        rootScale = 1 + e.hurtT * 0.5;
      }
      aw.rootPos = glm::vec3(static_cast<float>(rx), static_cast<float>(ry), static_cast<float>(rz));
      aw.rootYaw = yaw;
      v.human->animate(s, aw);
    } else {
      QuadrupedInput s;
      s.dt = dt;
      if (e.kind == proto::EntityKind::Mount) {
        s.mps = e.speed * 12.5;  // mount.js: speed = speedNow / 12,5
      } else {
        const bool windup = e.state == static_cast<std::uint8_t>(proto::EnemyState::Windup);
        const bool recover = e.state == static_cast<std::uint8_t>(proto::EnemyState::Recover);
        s.mps = e.speed;
        s.down = !alive;
        s.stun = (e.flags & proto::kFlagStun) != 0;
        s.windup = windup ? e.actionProgress : -1;
        s.attack = recover && e.actionProgress >= 0 ? e.actionProgress : -1;
        rootScale = 1 + e.hurtT * 0.5;
      }
      aw.rootPos = glm::vec3(static_cast<float>(rx), static_cast<float>(ry), static_cast<float>(rz));
      aw.rootYaw = yaw;
      v.quad->animate(s, aw);
    }
    if (!visible) continue;
    const glm::mat4 root = rootMatrix(rx, ry, rz, yaw, rootScale);
    const bool horseLike = v.quad && v.quad->kind() == QuadrupedAnimator::Kind::Mount;
    const float k = static_cast<float>(e.scale > 0 ? e.scale : 1.0);
    const glm::vec3 center(static_cast<float>(rx), static_cast<float>(ry) + (horseLike ? 1.2f : 1.0f * k), static_cast<float>(rz));
    emit(v, root, center, horseLike ? 3.0f : 1.8f * k + 0.6f, out);
  }
  for (auto it = views_.begin(); it != views_.end();) {
    if (it->second->seen != frame_) it = views_.erase(it);
    else ++it;
  }
}

}  // namespace rpg::client
