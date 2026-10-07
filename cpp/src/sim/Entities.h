#pragma once
// Entidades da simulação: os objetos de `G` da demo (G.player, G.enemies, G.projectiles, G.lootBags,
// NPCS, G.mount) sem nenhum modelo 3D. Cada campo tem o nome e o significado do JS correspondente;
// referências a outros objetos viram EntityId (ids nunca são reutilizados dentro de um mundo).
//
// Diferença de fundo: a demo tinha um único `G.player`. Aqui há N jogadores, e tudo o que era do
// jogador na demo (montaria, Livro, estatísticas, marcas, câmera de observação, incursão na Turbulenta)
// fica dentro de Player.
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/Math.h"
#include "core/Types.h"
#include "core/anim/AnimRules.h"
#include "core/data/Ids.h"
#include "core/movement/CharacterMotor.h"
#include "core/protocol/Interact.h"
#include "core/protocol/Messages.h"
#include "core/protocol/Replicated.h"
#include "core/world/MapId.h"
#include "core/world/ZoneMap.h"
#include "sim/Book.h"
#include "sim/Items.h"

namespace rpg::sim {

using EntityId = std::uint32_t;
inline constexpr EntityId kNoEntity = 0;

// Instância de mapa: a região é a 0; cada incursão na Turbulenta abre a sua.
using InstanceId = std::uint32_t;
inline constexpr InstanceId kRegionInstance = 0;

struct Vec3d {
  double x = 0, y = 0, z = 0;
};

// Origem de um golpe (combat.js `src`/`from`): posição e quem causou, para o relatório de derrota.
struct DamageSource {
  double x = 0, z = 0;
  bool hasPos = true;
  protocol::MessageId cause = protocol::MessageId::CauseWounds;  // quando não é um inimigo
  EnemyTypeId enemyType;                                         // válido: "Caiu diante de <nome>"
  std::optional<Faction> faction;
};

// ---------------------------------------------------------------- jogador
using protocol::PlayerState;

using anim::ActionKind;

struct BasicAct {
  ActionKind kind = ActionKind::Swing;
  double windup = 0, recover = 0;
  bool done = false;
  WeaponFamilyId family;
  bool aimed = false;
};

struct SkillAct {
  SkillId skill;
  std::set<EntityId> hitSet;
  bool done = false;
  bool swooshed = false;  // investida: `a.sw`
  int subStep = 0;
};

// O que acontece ao fim de uma canalização (as closures `onDone` da demo, agora como dado).
enum class ChannelAction : std::uint8_t {
  FieldRepair, Gather, Recon, MountUp, CallMount, CallMountNear, Extract, Shrine,
};

struct Channel {
  protocol::MessageId label = protocol::MessageId::ChannelFieldRepair;
  double dur = 0, t = 0;
  bool interruptible = true;
  bool anim = true;
  ChannelAction action = ChannelAction::FieldRepair;
  std::uint32_t target = 0;  // nó de coleta, santuário, saída…
};

// Comando de clique (pointer.js): atacar um inimigo ou ir até um alvo e usá-lo.
using protocol::InteractKind;

struct InteractRef {
  InteractKind kind = InteractKind::Market;
  std::uint32_t id = 0;  // índice do interagível estático, EntityId do saco/viajante, índice do santuário…
  bool operator==(const InteractRef&) const = default;
};

struct ClickCommand {
  enum class Type : std::uint8_t { Attack, Use } type = Type::Attack;
  EntityId enemy = kNoEntity;  // Attack
  bool inRange = false;        // Attack
  InteractRef target;          // Use
  double best = 0, since = 0;  // Use: desiste se não se aproximar por 1,2 s
};

struct Cooldowns {
  double q = 0, e = 0, pot = 0;
};

struct Equipment {
  std::optional<Gear> weapon, armor, bag, mount;
};

enum class GearSlot : std::uint8_t { Weapon, Armor, Bag, Mount };

// Montaria do jogador (mount.js G.mount): o animal existe no mundo mesmo sem ninguém montado.
struct MountState {
  EntityId id = kNoEntity;  // id replicado do animal
  bool present = false;
  XZ pos;
  double y = 0;
  double yaw = 0;
};

// Estado da incursão na Turbulenta (turbulent.js TS, a parte que é do jogador).
struct Shrine {
  double x = 0, z = 0;
  bool taken = false;
};
struct TurbExit {
  double x = 0, z = 0;
};
struct Encounter {
  EntityId shadow = kNoEntity;
  ItemId weapon;  // arma que a figura portava (o Livro só registra isso)
  enum class Result : std::uint8_t { None, Fell, StoodUp, NoOutcome } result = Result::None;
};
struct Incursion {
  bool inside = false;
  InstanceId instance = kRegionInstance;
  XZ origin;
  double enteredAt = 0, collapseAt = 0;
  int fragIn = 0;
  std::set<int> warned;
  bool lateSpawn = false;
  std::vector<Shrine> shrines;
  std::vector<TurbExit> exits;
  std::vector<EntityId> shadows;
  std::vector<Encounter> encounters;  // na ordem em que começaram (Map do JS)
};

// Contribuição de um personagem ao evento regional (event.js EVT.contrib / playerPts / delivered).
struct EventContribution {
  std::map<std::string, double> contrib;  // ferramentas, lingotes, reconhecimento, saqueadores, acampamento
  double pts = 0;
  std::map<std::string, int> delivered;   // ferramentas, lingote
  double get(const std::string& k) const {
    const auto it = contrib.find(k);
    return it == contrib.end() ? 0.0 : it->second;
  }
};

struct Player {
  EntityId id = kNoEntity;
  std::uint32_t session = 0;  // conexão dona (0 = nenhuma: jogador de teste)

  // perfil
  std::string name, origin, start, model;

  // corpo
  MapKind map = MapKind::Region;
  InstanceId instance = kRegionInstance;
  Vec3d pos;
  double yaw = kPi, aimYaw = kPi, aimPitch = 0, aimDist = 0;
  Vec3d aimPoint;
  EntityId aimEnemy = kNoEntity;

  // vitais
  double hp = 100, maxHp = 100, vigor = 100, maxVigor = 100, vigorDelay = 0;
  double coins = 40;
  std::vector<std::string> trainings;  // Set do JS: ordem de inserção
  std::vector<InvEntry> inv, saddle, storage, stableBags;
  Equipment equip;

  // estado
  PlayerState state = PlayerState::Free;
  double stateT = 0, invuln = 0, lastCombat = -99;
  bool mounted = false;
  std::optional<ClickCommand> cmd;
  bool aiming = false;
  Cooldowns cd;
  std::optional<Channel> channel;
  ZoneKind zone = ZoneKind::Protegida;
  PlaceId place = PlaceId::Mercado;
  std::optional<BasicAct> basic;  // P.act durante 'attack'
  std::optional<SkillAct> skill;  // P.act durante 'skill'
  double speedNow = 0;
  double metamorphT = 0, shieldHp = 0, shieldT = 0, auraVitalT = 0;

  // motor (pulo, agachar, pouso)
  bool crouch = false, running = false;
  double crouchW = 0, landT = 0, aimLock = 0;
  std::optional<motor::Air> air;
  double dodgeYaw = 0;
  double staggerT = 0.5;
  bool hasStaggerT = false;
  DamageSource downSrc;
  bool warnedBroken = false;
  // A arma aparece na mão (P.model.setWeapon da demo)? Só muda ao equipar, ao reaparecer e ao carregar
  // (inutilizada fica guardada) e no reparo; uma arma que quebra em combate continua à mostra.
  bool weaponShown = true;

  // reação ao golpe (só o animador lê)
  std::optional<double> hitAt;
  anim::HitSide hitDir = anim::HitSide::Front;

  // intenção de movimento do último passo (animação: passo lateral, para trás)
  double moveX = 0, moveZ = 0;
  bool moving = false;

  // coisas que eram globais na demo e são do personagem
  bool cinematic = false;  // G.cinematic: observando o céu
  bool anon = false;       // G.anon: identidade oculta na Turbulenta
  bool uiOpen = false;     // G.uiOpen: o cliente avisa pelo comando; bloqueia ações
  bool deathPanel = false; // G.uiOpen === 'death'
  MountState mount;
  Incursion turb;
  Book book;
  // G.stats: ordem de inserção (a demo lista os itens fabricados nessa ordem no Livro)
  std::vector<std::pair<std::string, double>> stats;
  std::set<std::string> flags;          // G.flags
  double discoveryT = 0;                // discovery.js `t`
  EventContribution evt;                // event.js EVT.contrib/playerPts/delivered
  std::set<int> observed;               // stars.js SKY.observed
};

// ---------------------------------------------------------------- inimigos
using protocol::EnemyState;

struct Telegraph {
  std::uint32_t id = 0;  // 0 = nenhum
};

struct Enemy {
  EntityId id = kNoEntity;
  EnemyTypeId type;
  Faction faction = Faction::Beast;
  MapKind map = MapKind::Region;
  InstanceId instance = kRegionInstance;
  bool alive = true;
  EnemyState state = EnemyState::Idle;
  double t = 0;
  Vec3d pos;
  double yaw = 0;
  double hp = 0, maxHp = 0;
  XZ home;
  double leash = 42;
  double radius = 0.45;
  std::uint32_t tele = 0;       // telegrafia ativa (id do evento), 0 = nenhuma
  double stun = 0;
  std::vector<InvEntry> carrying;
  std::int32_t group = -1;      // índice do grupo de reaparecimento
  double barY = 2.3;
  double hurtT = 0;
  std::optional<XZ> wander;
  double cd = 0;
  struct Post {
    double x = 0, z = 0;
    std::optional<double> yaw;
  };
  std::optional<Post> post;
  std::vector<XZ> patrol;
  int pi = 0;
  EntityId targetBag = kNoEntity;
  EntityId target = kNoEntity;  // jogador perseguido
  double lockYaw = 0;
  XZ origin;
  double atk = -1;              // progresso do golpe para o animador (-1 = nenhum); guarda: `atk` indefinido
  bool hasAtk = false;
  double slowT = 0, rootT = 0, burnT = 0, burnTick = 0, curseT = 0;
  EntityId burnBy = kNoEntity;  // quem pôs fogo (recebe o abate)
  std::optional<double> hitAt;
  anim::HitSide hitDir = anim::HitSide::Front;
  anim::DeathFall deathDir = anim::DeathFall::Backward;
  bool hasDeathDir = false;
  bool camp = false;            // membro do acampamento reativo
  bool guardPost = false;
  bool hidden = false;          // e.hidden (a demo nunca liga; mantido pela fidelidade)
  bool asleep = false;          // `root.visible = false` da demo: longe e parado
  bool barShown = false;        // showBar/removeBar
  double speedNow = 0;          // v do último passo (animação)
};

// ---------------------------------------------------------------- projéteis
using protocol::ProjectileKind;
using protocol::ProjectileOwner;

struct Projectile {
  EntityId id = kNoEntity;
  MapKind map = MapKind::Region;
  InstanceId instance = kRegionInstance;
  double x = 0, y = 0, z = 0, dx = 0, dy = 0, dz = 0;
  double speed = 0, dmg = 0;
  ProjectileOwner owner = ProjectileOwner::Enemy;
  EntityId shooter = kNoEntity;  // jogador que disparou (recompensas e desgaste)
  bool pierce = false;
  double left = 45;
  std::set<EntityId> hit;
  double ox = 0, oy = 0, oz = 0;
  SpellSchool school = SpellSchool::None;
  ProjectileKind kind = ProjectileKind::Arrow;
  bool ballistic = false;
};

// ---------------------------------------------------------------- cargas no chão
using protocol::LootLabel;

struct LootBag {
  EntityId id = kNoEntity;
  MapKind map = MapKind::Region;
  InstanceId instance = kRegionInstance;
  double x = 0, z = 0;
  std::vector<InvEntry> items;
  LootLabel label = LootLabel::Cargo;
  bool own = false;
  EntityId owner = kNoEntity;  // de quem é a carga (`own` do ponto de vista dele)
  EntityId claimed = kNoEntity;
  double expires = 0;
  PlaceId place = PlaceId::Mercado;
  ZoneKind zone = ZoneKind::Protegida;
};

// ---------------------------------------------------------------- NPCs
enum class NpcRole : std::uint8_t { Merchant, Smith, Trainer, Stable, Guard, Foreman, Astro, Astronomer, Traveler };

struct Npc {
  EntityId id = kNoEntity;
  std::uint32_t layoutIndex = 0;  // entrada em GameplayLayout::npcs (aparência)
  NpcRole role = NpcRole::Merchant;
  bool face = false;
  bool work = false;
  Vec3d pos;
  double yaw = 0;
  // viajantes
  int traveler = -1;  // índice em GameplayLayout::travelers
  int seg = 0, dir = 1;
  double wait = 0;
  double speedNow = 0;
};

}  // namespace rpg::sim
