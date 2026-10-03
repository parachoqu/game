#pragma once
// Definições de design (regras) carregadas de cpp/data/*.json, geradas a partir de demo/src/config.js
// por demo/tools/export-game-data.mjs. Só entram campos que a simulação usa: nomes, descrições,
// glifos e cores ficam em data/text e data/presentation, lidos apenas pelo cliente.
#include <optional>
#include <utility>
#include <vector>

#include "core/GameClock.h"
#include "core/data/Ids.h"

namespace rpg {

// ---------------------------------------------------------------- itens (ITEMS)
enum class ItemKind : std::uint8_t { Material, Goods, Consumable, Weapon, Armor, Bag, Mount };

struct ItemDef {
  double weight = 0.0;            // kg (`w`)
  ItemKind kind = ItemKind::Material;
  WeaponFamilyId family;          // armas: família em WEAPONS
  TrainingId requiredTraining;    // treino exigido para equipar (`req`); inválido = livre
  int armorHp = 0;                // armaduras: vida extra (`hp`)
  double armorSpeed = 1.0;        // armaduras: multiplicador de deslocamento (`speed`)
  double capacity = 0.0;          // bolsa e montaria: capacidade em kg (`cap`)

  // config.js GEAR_KINDS: equipamentos têm uid e condição, materiais empilham.
  bool isGear() const {
    return kind == ItemKind::Weapon || kind == ItemKind::Armor || kind == ItemKind::Bag || kind == ItemKind::Mount;
  }
};

// ---------------------------------------------------------------- armas e técnicas (WEAPONS, SKILLS)
enum class AttackType : std::uint8_t { Melee, Ranged, Spell };
enum class SpellSchool : std::uint8_t { None, Ice, Fire, Nature, Wind, Curse, Vital, Holy };

struct BasicAttack {
  AttackType type = AttackType::Melee;
  double damage = 0.0;
  double range = 0.0;    // corpo a corpo: alcance (m)
  double arc = 0.0;      // corpo a corpo: abertura (rad)
  double windup = 0.0;   // s até o golpe sair
  double recover = 0.0;  // s de recuperação depois do golpe
  double speed = 0.0;    // projéteis: m/s
  SpellSchool school = SpellSchool::None;
};

struct WeaponDef {
  BasicAttack basic;
  std::vector<SkillId> skills;  // técnica Q = [0], E = [1]; os punhos não têm
};

struct SkillDef {
  double cost = 0.0;      // vigor
  double cooldown = 0.0;  // s
};

struct TrainingDef {
  int cost = 0;  // moedas
};

// ---------------------------------------------------------------- fabricação (RECIPES)
struct RecipeDef {
  ItemId out;
  int quantity = 1;
  std::vector<std::pair<ItemId, int>> inputs;  // na ordem do config.js
  TrainingId requiredTraining;                 // inválido = sem requisito
};

// ---------------------------------------------------------------- inimigos (ENEMIES + guarda)
enum class Faction : std::uint8_t { Beast, Bandit, Shadow, Guard };
enum class AttackShape : std::uint8_t { Sector, Circle };  // à distância a telegrafia vira linha

struct DropRange {
  ItemId item;
  int min = 0;
  int max = 0;
};

struct EnemyDef {
  Faction faction = Faction::Beast;
  double hp = 0.0;
  double speed = 0.0;
  double damage = 0.0;
  double range = 0.0;
  double arc = 1.4;  // o JS usa `cfg.arc || 1.4`
  AttackShape shape = AttackShape::Sector;
  double windup = 0.0;
  double cooldown = 0.0;
  double aggro = 0.0;  // raio em que passa a perseguir
  std::vector<DropRange> drops;
  std::optional<std::pair<int, int>> coins;
  bool ranged = false;
  WeaponFamilyId weapon;  // humanoides: arma na mão (e o tipo de ataque à distância)
  double scale = 1.0;     // o JS usa `cfg.scale || 1`
};

// ---------------------------------------------------------------- zonas (ZONES)
// Matriz de derrota (zones.js `handleDeath`): protegida só desgasta, fronteira deixa a carga, Full Loot
// e Turbulenta deixam tudo o que é levado (e parte é destruída).
enum class LossRule : std::uint8_t { Wear, Cargo, Full };

struct ZoneDef {
  LossRule loss = LossRule::Wear;
};

// ---------------------------------------------------------------- mercados (MARKETS)
struct MarketDef {
  // Indexados por ItemId: preço de compra (o jogador paga) e preço-base de venda (o jogador recebe,
  // antes da demanda e da condição). Vazio = o mercado não negocia o item.
  std::vector<std::optional<double>> buy;
  std::vector<std::optional<double>> sell;

  std::optional<double> buyPrice(ItemId id) const { return id.value < buy.size() ? buy[id.value] : std::nullopt; }
  std::optional<double> sellBase(ItemId id) const { return id.value < sell.size() ? sell[id.value] : std::nullopt; }
};

// ---------------------------------------------------------------- criação de personagem
struct OriginDef {};  // as origens só mudam aparência e textos

struct StartDef {
  ItemId weapon;
  // A chave do arquétipo entra nos treinos do personagem (player.js createPlayer), mesmo quando não é
  // um treino de TRAININGS (lanceiro, mago_gelo, piromante, metamorfo). `training` aponta para o treino
  // quando ele existe; o C++ guarda a chave de qualquer forma para reproduzir a demo.
  TrainingId training;
};

// ---------------------------------------------------------------- constantes de regra
struct Balance {
  ClockConfig clock;
  struct Movement {
    double walk = 0.0, run = 0.0, crouch = 0.0, gravity = 0.0, airControl = 0.0;
    struct Jump {
      double vy = 0.0, vigor = 0.0;
    } jump;
    struct Boost {
      double vy = 0.0, vigor = 0.0, speed = 0.0;
    } boost;
  } movement;
  struct Collision {
    double cell = 0.0, maxSlope = 0.0, routeSlope = 0.0, routeLane = 0.0;
  } collision;
  struct Inventory {
    double baseCapacity = 0.0, overLimit = 0.0;
  } inventory;
  struct Turbulent {
    double duration = 0.0;
  } turbulent;
};

}  // namespace rpg
