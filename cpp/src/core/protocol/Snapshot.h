#pragma once
// Estado do mundo como o cliente o vê (canal Snapshots, não confiável: cada um substitui o anterior).
//
//   EntityState  — o que se desenha de cada entidade perto do jogador (corpo, pose, animação)
//   PrivateState — o que só o dono vê (vitais, inventário, canalização, cooldowns)
//   WorldState   — estado compartilhado que as telas mostram (relógio, evento, acampamento, céu,
//                  portal, mercados, pontos de coleta)
//
// No jogo local o snapshot vai inteiro a cada passo. A compressão por diferença contra o último
// snapshot confirmado entra na fase online.
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/protocol/ByteStream.h"

namespace rpg::protocol {

inline constexpr std::uint16_t kProtocolVersion = 1;

enum class EntityKind : std::uint8_t { Player, Enemy, Npc, Mount, Projectile, LootBag };

enum EntityFlag : std::uint32_t {
  kFlagCrouch = 1u << 0,
  kFlagMounted = 1u << 1,
  kFlagMetamorph = 1u << 2,
  kFlagChannelAnim = 1u << 3,  // canalização com gesto de trabalho
  kFlagStun = 1u << 4,
  kFlagBar = 1u << 5,          // barra de vida visível
  kFlagAsleep = 1u << 6,       // longe de todos e parado (não anima)
  kFlagAnon = 1u << 7,         // identidade oculta (Turbulenta, sombras)
  kFlagDown = 1u << 8,         // derrubado ou morto
  kFlagBlink = 1u << 9,        // cintilando (invulnerável após reaparecer)
  kFlagAirBoosted = 1u << 10,
  kFlagMoving = 1u << 11,
  kFlagWork = 1u << 12,        // NPC trabalhando (forja)
  kFlagOwn = 1u << 13,         // saco de carga do próprio jogador
  kFlagAlive = 1u << 14,
  kFlagBallistic = 1u << 15,
  kFlagHuman = 1u << 16,       // inimigo humanoide (os demais usam o animador de fera)
};

struct EntityState {
  std::uint32_t id = 0;
  EntityKind kind = EntityKind::Player;
  std::uint8_t map = 0;
  std::uint32_t instance = 0;
  double x = 0, y = 0, z = 0, yaw = 0;
  std::uint32_t flags = 0;
  std::uint16_t type = 0;         // inimigo: EnemyTypeId; NPC: índice no layout; projétil: tipo e escola
  std::uint16_t weapon = 0xFFFF;  // família da arma na mão (WeaponFamilyId), 0xFFFF = nenhuma
  std::uint8_t state = 0;         // estado do jogador / inimigo
  double speed = 0;               // m/s do passo (locomoção)
  double stateT = 0;
  std::uint8_t action = 0;        // ActionKind
  double actionProgress = -1;     // -1 = nenhuma ação (ataque, técnica, preparo)
  double dodgeYaw = 0;
  double dodgeProgress = -1;
  std::uint8_t hitSide = 0;
  double hitAge = -1;             // s desde o último golpe (-1 = nenhum recente)
  std::uint8_t deathFall = 0;
  double deadT = 0;
  double aimPitch = 0;
  double airPhase = -1;
  double moveX = 0, moveZ = 0;
  double hpFrac = 1;
  double hurtT = 0;               // pulso de dano
  double scale = 1;
  // projéteis: direção
  double dx = 0, dy = 0, dz = 0;
  // jogadores: identidade (vazia quando anônimo)
  std::string name, origin, model;

  template <class A>
  void io(A& a) {
    a(id); a(kind); a(map); a(instance);
    a.f32(x); a.f32(y); a.f32(z); a.f32(yaw);
    a(flags); a(type); a(weapon); a(state);
    a.f32(speed); a.f32(stateT); a(action); a.f32(actionProgress);
    a.f32(dodgeYaw); a.f32(dodgeProgress); a(hitSide); a.f32(hitAge); a(deathFall); a.f32(deadT);
    a.f32(aimPitch); a.f32(airPhase); a.f32(moveX); a.f32(moveZ); a.f32(hpFrac); a.f32(hurtT); a.f32(scale);
    a.f32(dx); a.f32(dy); a.f32(dz);
    a(name); a(origin); a(model);
  }
};

struct ItemEntry {
  std::uint16_t item = 0;
  std::int32_t qty = 1;
  std::uint32_t uid = 0;
  double cond = 100;
  template <class A> void io(A& a) { a(item); a(qty); a(uid); a.f32(cond); }
};

struct ChannelState {
  std::uint16_t label = 0;  // MessageId
  double t = 0, dur = 0;
  template <class A> void io(A& a) { a(label); a.f32(t); a.f32(dur); }
};

struct TurbState {
  bool inside = false;
  double enteredAt = 0, collapseAt = 0;
  std::vector<std::uint8_t> shrinesTaken;
  template <class A> void io(A& a) { a(inside); a(enteredAt); a(collapseAt); a(shrinesTaken); }
};

struct PrivateState {
  std::uint32_t playerId = 0;
  std::uint32_t lastCommandSeq = 0;  // último comando aplicado (reconciliação)
  double hp = 0, maxHp = 0, vigor = 0, maxVigor = 0, coins = 0;
  std::uint8_t state = 0;
  double stateT = 0, invuln = 0;
  double cdQ = 0, cdE = 0, cdPot = 0;
  std::optional<ChannelState> channel;
  std::uint8_t zone = 0, place = 0, load = 0;
  double bagWeight = 0, capacity = 0, saddleWeight = 0, saddleCapacity = 0;
  bool saddleReachable = false;
  std::vector<ItemEntry> inv, saddle, storage, stableBags;
  std::optional<ItemEntry> weapon, armor, bag, mount;
  std::vector<std::string> trainings;
  double metamorphT = 0, shieldHp = 0, shieldT = 0, auraVitalT = 0;
  bool mounted = false, aiming = false, crouch = false, inCombat = false, cinematic = false, anon = false;
  std::optional<double> airY;
  double lastCombat = -99;
  // alvo do clique (anel de seleção): inimigo ou alvo de uso
  std::uint32_t attackTarget = 0;
  bool mountPresent = false;
  double mountX = 0, mountZ = 0;
  TurbState turb;
  std::map<std::string, double> stats;
  std::set<std::string> flags;
  // motor (predição): estado que o CharacterMotor lê
  double landT = 0, crouchW = 0, aimLock = 0;
  double airVy = 0, airVx = 0, airVz = 0, airT = 0, airDur = 0;
  bool airBoosted = false;

  template <class A>
  void io(A& a) {
    a(playerId); a(lastCommandSeq);
    a(hp); a(maxHp); a(vigor); a(maxVigor); a(coins);
    a(state); a(stateT); a(invuln); a(cdQ); a(cdE); a(cdPot); a(channel);
    a(zone); a(place); a(load); a.f32(bagWeight); a.f32(capacity); a.f32(saddleWeight); a.f32(saddleCapacity);
    a(saddleReachable); a(inv); a(saddle); a(storage); a(stableBags); a(weapon); a(armor); a(bag); a(mount);
    a(trainings); a(metamorphT); a(shieldHp); a(shieldT); a(auraVitalT);
    a(mounted); a(aiming); a(crouch); a(inCombat); a(cinematic); a(anon); a(airY); a(lastCombat);
    a(attackTarget); a(mountPresent); a.f32(mountX); a.f32(mountZ); a(turb); a(stats); a(flags);
    a(landT); a(crouchW); a(aimLock); a(airVy); a(airVx); a(airVz); a(airT); a(airDur); a(airBoosted);
  }
};

struct VanishedStar {
  std::int32_t index = 0, constellation = -1;
  double time = 0;
  template <class A> void io(A& a) { a(index); a(constellation); a(time); }
};

struct MarketState {
  std::map<std::uint16_t, double> demand;      // item → procura (ausente = 1)
  std::map<std::uint16_t, double> sellBase;    // preço-base de venda atual (muda com a reabertura)
  template <class A> void io(A& a) { a(demand); a(sellBase); }
};

struct NodeState {
  std::uint8_t charges = 3;
  double regrowAt = 0;
  template <class A> void io(A& a) { a(charges); a(regrowAt); }
};

struct WorldState {
  std::uint64_t tick = 0;
  double time = 0;
  bool paused = false;
  // evento regional (event.js EVT)
  double evtProgress = 0;
  bool evtDone = false;
  std::map<std::string, double> evtContrib;  // do jogador que recebe
  double evtPlayerPts = 0, evtOthersPts = 0, evtTotalPts = 0;
  // acampamento (camp.js CAMP)
  std::uint8_t campState = 0;
  // céu (stars.js SKY)
  std::vector<VanishedStar> vanished;
  std::set<std::int32_t> observed;
  bool instrumentGiven = false;
  // portal (turbulent.js TS.portal)
  bool portal = false;
  std::uint32_t portalId = 0;  // id do alvo de uso (ReqClickUse{Portal, portalId})
  double portalX = 0, portalZ = 0, portalExpires = 0;
  std::uint8_t portalPlace = 0;
  std::vector<MarketState> markets;
  std::vector<NodeState> nodes;

  template <class A>
  void io(A& a) {
    a(tick); a(time); a(paused);
    a(evtProgress); a(evtDone); a(evtContrib); a(evtPlayerPts); a(evtOthersPts); a(evtTotalPts);
    a(campState); a(vanished); a(observed); a(instrumentGiven);
    a(portal); a(portalId); a(portalX); a(portalZ); a(portalExpires); a(portalPlace);
    a(markets); a(nodes);
  }
};

struct Snapshot {
  WorldState world;
  std::optional<PrivateState> self;  // ausente antes de o personagem existir
  std::vector<EntityState> entities;
  template <class A> void io(A& a) { a(world); a(self); a(entities); }
};

}  // namespace rpg::protocol
