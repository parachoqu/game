#pragma once
// Todos os dados de design carregados e validados. Servidor e cliente carregam o mesmo diretório
// (cpp/data); o cliente lê à parte os textos e a aparência.
#include <filesystem>

#include "core/data/DefTable.h"
#include "core/data/Defs.h"

namespace rpg {

struct GameData {
  DefTable<TrainingDef, TrainingId> trainings;
  DefTable<SkillDef, SkillId> skills;
  DefTable<WeaponDef, WeaponFamilyId> weapons;
  DefTable<ItemDef, ItemId> items;
  DefTable<EnemyDef, EnemyTypeId> enemies;
  DefTable<ZoneDef, ZoneId> zones;
  DefTable<MarketDef, MarketId> markets;
  DefTable<RecipeDef, RecipeId> recipes;
  DefTable<OriginDef, OriginId> origins;
  DefTable<StartDef, StartId> starts;
  Balance balance;
  double mountPrice = 0.0;       // config.js MOUNT_PRICE
  double restartKitPrice = 0.0;  // config.js RESTART_KIT_PRICE

  // Lê `dir`/*.json. Qualquer campo ausente, tipo errado ou referência cruzada inválida (receita que
  // aponta para item inexistente, arma com técnica desconhecida…) vira DataError com arquivo e chave.
  static GameData load(const std::filesystem::path& dir);
};

}  // namespace rpg
