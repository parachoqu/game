#pragma once
// Estado da simulação (o `G` de state.js e os estados de módulo EVT, CAMP, SKY, TS, MKT, NPCS, NODES,
// groups) e as funções dos sistemas. Interno à biblioteca rpg_sim: quem está fora usa sim/World.h.
//
// Cada sistema é um conjunto de funções livres que recebem `Sim&`, como os módulos da demo recebiam
// `G` implicitamente. Os nomes seguem os do JS (updatePlayer, hurtEnemy, handleDeath…), e a ordem
// das operações dentro de cada função é a mesma: é isso que mantém o rastro igual ao da demo.
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/Random.h"
#include "core/data/GameData.h"
#include "core/protocol/GameEvents.h"
#include "core/protocol/PlayerCommand.h"
#include "core/world/GameplayLayout.h"
#include "core/world/SkyLayout.h"
#include "core/world/StaticWorld.h"
#include "sim/Entities.h"

namespace rpg::sim {

// ---------------------------------------------------------------- estados de mundo
using protocol::CampPhase;

struct CampState {  // camp.js CAMP
  int pop = 9;
  CampPhase state = CampPhase::Estabelecido;
  double since = 0;
  bool pending = true;
  std::vector<EntityId> members;
  int playerKills = 0;  // abates por jogadores desde a última mudança de estado
  double regenT = 0;
};

struct EventState {  // event.js EVT (parte do mundo)
  double progress = 0;
  bool done = false;
  std::optional<double> doneAt;
  double othersPts = 0, othersT = 0;
  double playersPts = 0;  // soma dos pontos de todos os personagens
};

struct VanishedStar {
  int index = 0, constellation = -1;
  double time = 0;
};

struct SkyState {  // stars.js SKY (parte do mundo)
  std::vector<VanishedStar> vanished;
  double next = 50;
  bool instrumentGiven = false;
};

struct Portal {
  std::uint32_t id = 0;
  double x = 0, z = 0, expires = 0;
  PlaceId place = PlaceId::Mercado;
};

struct TurbWorld {  // turbulent.js TS (parte do mundo)
  std::optional<Portal> portal;
  double next = 75;
};

struct NodeRuntime {  // world.js NODES
  int charges = 3, max = 3;
  double regrowAt = 0;
};

struct GroupRuntime {  // enemies.js groups
  int layoutIndex = 0;
  std::vector<EntityId> list;
  std::optional<double> deadAt;
};

// ---------------------------------------------------------------- saída de eventos
struct Recipient {
  enum class Type : std::uint8_t { Player, Near, Instance, All } type = Type::All;
  EntityId player = kNoEntity;
  MapKind map = MapKind::Region;
  InstanceId instance = kRegionInstance;
  double x = 0, z = 0, radius = 0;
};

struct OutEvent {
  Recipient to;
  protocol::GameEvent event;
};

// Opções de simulação para testes e harness de paridade.
struct SimOptions {
  bool populate = true;       // grupos, acampamento e guarda do canteiro ao criar o mundo
  bool travelers = true;      // viajantes nas estradas
  bool portals = true;        // portais surgem sozinhos (turbulent.js exige G.running)
  // sistemas rodados a cada passo (os testes de paridade ligam só os que o roteiro da demo roda)
  bool stepMount = true, stepEnemies = true, stepGroups = true, stepProjectiles = true, stepCamp = true,
       stepEvent = true, stepStars = true, stepTurbulent = true, stepMarkets = true, stepLootBags = true,
       stepNpcs = true, stepInteract = true, stepDiscovery = true;
};

// ---------------------------------------------------------------- o estado
struct Sim {
  Sim(const GameData& d, const StaticWorld& s, const GameplayLayout& l, std::unique_ptr<IRandom> r, SimOptions o);

  const GameData& data;
  const StaticWorld& statics;
  const GameplayLayout& layout;
  SkyLayout sky;
  std::unique_ptr<IRandom> rng;
  SimOptions opts;

  // tempo
  double time = 0;
  std::uint64_t tick = 0;
  double nightNow = 0;  // G.nightNow: luz do passo anterior
  bool running = true;  // G.running

  // entidades (ordem de inserção = ordem dos arrays da demo)
  std::vector<std::unique_ptr<Player>> players;
  std::vector<std::unique_ptr<Enemy>> enemies;
  std::vector<std::unique_ptr<Projectile>> projectiles;
  std::vector<std::unique_ptr<LootBag>> lootBags;
  std::vector<Npc> npcs;

  // mundo
  CampState camp;
  EventState evt;
  SkyState skyState;
  TurbWorld turb;
  std::vector<std::map<ItemId, double>> demand;         // MKT[mk].demand, por MarketId
  std::vector<std::vector<std::optional<double>>> sell;  // MARKETS[mk].sell, mutável (reabertura)
  std::vector<NodeRuntime> nodes;
  std::vector<GroupRuntime> groups;
  UidAllocator uids;

  // saída
  std::vector<OutEvent> out;
  EntityId nextId = 1;
  std::uint32_t nextTelegraph = 1;
  InstanceId nextInstance = 1;
  std::uint32_t nextPortal = 1;

  // ---------------------------------------------------------------- utilidades
  EntityId newId() { return nextId++; }
  const StaticMap& mapOf(MapKind k) const { return statics.map(k); }
  Player* player(EntityId id);
  const Player* player(EntityId id) const;
  Enemy* enemy(EntityId id);
  const Enemy* enemy(EntityId id) const;
  LootBag* lootBag(EntityId id);
  const Npc* npc(EntityId id) const;
  Player* nearestPlayer(MapKind map, InstanceId inst, double x, double z, double* dist = nullptr);

  int day() const;

  // eventos
  void emit(Recipient to, protocol::GameEvent e) { out.push_back({to, std::move(e)}); }
  void toPlayer(const Player& p, protocol::GameEvent e);
  void toNear(MapKind map, InstanceId inst, double x, double z, protocol::GameEvent e, double radius = 90);
  void sfx(const Player& p, protocol::SoundId s);  // som que só o dono ouve (interface)
  void sfxAt(MapKind map, InstanceId inst, double x, double z, protocol::SoundId s);
  void toast(const Player& p, protocol::MessageId m, protocol::MsgArgs args = {},
             protocol::ToastKind k = protocol::ToastKind::Info, std::uint16_t ms = 0);
  void floatText(MapKind map, InstanceId inst, double x, double y, double z, protocol::FloatKind k,
                 protocol::MessageId m, protocol::MsgArgs args = {});
  void burst(MapKind map, InstanceId inst, double x, double y, double z, std::uint32_t color, int n, double speed);
  std::uint32_t telegraph(MapKind map, InstanceId inst, protocol::EvTelegraph t);
  void cancelTelegraph(MapKind map, InstanceId inst, double x, double z, std::uint32_t& id);
  void bookEvent(Player& p, std::optional<int> change);
};

// ---------------------------------------------------------------- inventário (inventory.js)
double capacity(const Sim& s, const Player& p);
double saddleCap(const Sim& s, const Player& p);
double loadRatio(const Sim& s, const Player& p);
motor::LoadState loadState(const Sim& s, const Player& p);
bool saddleReachable(const Player& p);
int receive(Sim& s, Player& p, ItemId id, int qty = 1);
bool haveAll(const Player& p, const RecipeDef& r, bool useStorage);
void consume(Player& p, const RecipeDef& r, bool useStorage);
std::vector<Gear*> equippedList(Player& p);
double armorHp(const Sim& s, const Player& p);
double armorSpeed(const Sim& s, const Player& p);
void wear(std::optional<Gear>& g, double amt);
WeaponFamilyId weaponFamily(const Sim& s, const Player& p);
bool hasTraining(const Player& p, std::string_view key);

// ---------------------------------------------------------------- estatísticas e Livro (state.js, book.js)
double stat(Player& p, const std::string& key, double n = 1);
double statOf(const Player& p, std::string_view key);  // 0 se ausente
void bookFirst(Sim& s, Player& p, std::string key, BookCat cat, BookDomain dom, std::string tmpl,
               protocol::MsgArgs args = {});
void bookTally(Sim& s, Player& p, std::string key, BookCat cat, BookDomain dom, std::string tmpl, int inc = 1,
               std::optional<TallyData> data = std::nullopt, protocol::MsgArgs args = {});
void bookLog(Sim& s, Player& p, BookCat cat, BookDomain dom, std::string tmpl, protocol::MsgArgs args,
             std::string key = {});
void grantTitle(Sim& s, Player& p, const std::string& id);

// ---------------------------------------------------------------- jogador (player.js)
struct PlayerProfile {
  std::string name, origin, start, model;
};
Player& createPlayer(Sim& s, const PlayerProfile& profile, std::uint32_t session);
void removePlayer(Sim& s, EntityId id);
void stagger(Player& p, double t);
void startChannel(Player& p, protocol::MessageId label, double dur, ChannelAction action, std::uint32_t target = 0,
                  bool interruptible = true, bool anim = true);
void cancelChannel(Sim& s, Player& p, std::optional<protocol::MessageId> reason);
void knockDown(Sim& s, Player& p, const DamageSource& src);
void dismount(Sim& s, Player& p, bool forced);
void mountUp(Sim& s, Player& p);
void drinkPotion(Sim& s, Player& p);
bool inCombat(const Sim& s, const Player& p);
// Um passo do jogador com o comando do passo (edges = toques novos desde o último comando).
struct PlayerInput {
  protocol::PlayerCommand cmd;
  std::array<bool, static_cast<std::size_t>(protocol::Press::Count)> pressed{};
  bool hit(protocol::Press p) const { return pressed[static_cast<std::size_t>(p)]; }
};
void updatePlayer(Sim& s, Player& p, const PlayerInput& in, double dt);
void applyAim(Player& p, const protocol::PlayerCommand& cmd);

// ---------------------------------------------------------------- combate (combat.js)
struct HurtOpts {
  std::optional<XZ> from;
  double knock = 0;
  bool big = false;
  std::optional<std::uint32_t> burstColor;
  double slow = 0, root = 0, burn = 0, curse = 0, healPlayer = 0, stun = 0;
  bool byPlayer = true;
  EntityId attacker = kNoEntity;  // jogador que recebe o crédito
  bool dot = false;
};
void hurtPlayer(Sim& s, Player& p, double dmg, const DamageSource& src);
bool hurtEnemy(Sim& s, Enemy& e, double dmg, const HurtOpts& opts);
void killEnemy(Sim& s, Enemy& e, bool byPlayer, EntityId attacker);
int meleeSweep(Sim& s, Player& attacker, double ox, double oz, double yaw, double range, double arc, double dmg,
               HurtOpts opts);
struct ShotSpec {
  double x = 0, y = 0, z = 0, yaw = 0, speed = 0, dmg = 0;
  ProjectileOwner owner = ProjectileOwner::Enemy;
  EntityId shooter = kNoEntity;
  double range = 45;
  ProjectileKind kind = ProjectileKind::Arrow;
  SpellSchool school = SpellSchool::None;
  bool pierce = false;
  std::optional<Vec3d> target;
  MapKind map = MapKind::Region;
  InstanceId instance = kRegionInstance;
};
void shoot(Sim& s, const ShotSpec& o);
void updateProjectiles(Sim& s, double dt);

// ---------------------------------------------------------------- inimigos (enemies.js)
struct SpawnOpts {
  std::optional<double> leash;
  std::int32_t group = -1;
  std::optional<Enemy::Post> post;
  std::vector<XZ> patrol;
  MapKind map = MapKind::Region;
  InstanceId instance = kRegionInstance;
};
Enemy& spawnEnemy(Sim& s, EnemyTypeId type, double x, double z, const SpawnOpts& o = {});
void removeEnemy(Sim& s, EntityId id);
void defineGroups(Sim& s);
void updateGroups(Sim& s);
void updateEnemies(Sim& s, double dt);

// ---------------------------------------------------------------- zonas e cargas (zones.js)
struct LootBagOpts {
  bool own = false;
  EntityId owner = kNoEntity;
  double ttl = 300;
};
LootBag& createLootBag(Sim& s, MapKind map, InstanceId inst, double x, double z, std::vector<InvEntry> items,
                       LootLabel label, const LootBagOpts& o = {});
void removeLootBag(Sim& s, EntityId id);
void updateLootBags(Sim& s);
protocol::DeathReport handleDeath(Sim& s, Player& p, const DamageSource& src);
void respawn(Sim& s, Player& p);
ZoneKind zoneOf(const Sim& s, MapKind map, double x, double z);
PlaceId placeOf(const Sim& s, MapKind map, double x, double z);

// ---------------------------------------------------------------- montaria (mount.js)
void mountAction(Sim& s, Player& p);
void spawnMountNear(Sim& s, Player& p);
void despawnMount(Sim& s, Player& p);
void updateMount(Sim& s, Player& p);

// ---------------------------------------------------------------- economia (economy.js)
std::optional<double> sellPrice(const Sim& s, MarketId mk, ItemId id, const InvEntry* entry);
std::optional<double> buyPrice(const Sim& s, MarketId mk, ItemId id);
double demandOf(const Sim& s, MarketId mk, ItemId id);
void updateMarkets(Sim& s, double dt);
double sell(Sim& s, Player& p, MarketId mk, ItemId id, int qty);
int buy(Sim& s, Player& p, MarketId mk, ItemId id, int qty);
void buyMount(Sim& s, Player& p);
void restartKit(Sim& s, Player& p);
void learn(Sim& s, Player& p, TrainingId id);
std::optional<protocol::MessageId> craftCheck(const Sim& s, const Player& p, const RecipeDef& r, bool useStorage);
bool craft(Sim& s, Player& p, RecipeId id, bool useStorage);
struct RepairCost {
  double coins = 0;
  int lingote = 0;
};
RepairCost repairCost(const Gear& g);
void repair(Sim& s, Player& p, GearSlot slot);

// ---------------------------------------------------------------- interação (interact.js)
struct Target {
  InteractRef ref;
  double x = 0, z = 0, r = 0, bias = 0;
  bool runnable = true;
};
std::vector<Target> targetsNear(Sim& s, const Player& p, double maxDist);
std::optional<Target> nearestInRange(Sim& s, const Player& p);
std::optional<Target> resolveTarget(Sim& s, const Player& p, const InteractRef& ref);
bool targetValid(Sim& s, const Player& p, const InteractRef& ref);
void runTarget(Sim& s, Player& p, const InteractRef& ref);
void updateInteract(Sim& s, Player& p, bool pressInteract);
void regrowNodes(Sim& s);
void finishChannel(Sim& s, Player& p, const Channel& c);

// ---------------------------------------------------------------- sistemas do mundo
void initCamp(Sim& s);
void onEnemyKilledCamp(Sim& s, Enemy& e, bool byPlayer, EntityId attacker);
void updateCamp(Sim& s, double dt);
void contribute(Sim& s, Player* p, const std::string& kind, double pts);
void deliver(Sim& s, Player& p, ItemId id);
void recon(Sim& s, Player& p);
void onEnemyKilledEvent(Sim& s, Enemy& e, bool byPlayer, EntityId attacker);
void updateEvent(Sim& s, double dt);
void applyReopenEffects(Sim& s);
double eventShare(const Sim& s, const Player& p);
void updateStars(Sim& s, double dt, double night);
void observe(Sim& s, Player& p, double night);
void endObserve(Sim& s, Player& p);
void giveInstrument(Sim& s, Player& p);
void updateTurbulent(Sim& s, double dt);
void forcePortalNear(Sim& s, Player& p);
std::optional<protocol::MessageId> canEnter(const Player& p);
// panels.js `lootPanel`: o conteúdo do saco para o painel de quem o abriu (vazio se ele sumiu).
void sendLootView(Sim& s, const Player& p, EntityId bag);
void enterTurbulent(Sim& s, Player& p);
void leaveTurbulent(Sim& s, Player& p, bool success);
void onTurbulentDeath(Sim& s, Player& p, const DamageSource& src);
void initNpcs(Sim& s);
void updateNpcs(Sim& s, double dt);
void updateDiscovery(Sim& s, Player& p, double dt);

// ---------------------------------------------------------------- utilidades de ids de dados
ItemId item(const Sim& s, std::string_view key);
EnemyTypeId enemyType(const Sim& s, std::string_view key);

}  // namespace rpg::sim
