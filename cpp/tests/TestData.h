#pragma once
// Dados reais carregados uma vez para todos os testes (cpp/data e cpp/assets/sim).
#include "core/Paths.h"
#include "core/data/GameData.h"
#include "core/world/StaticWorld.h"

namespace rpg::test {

inline const GameData& gameData() {
  static const GameData d = GameData::load(pathFromUtf8(RPG_TEST_DATA_DIR));
  return d;
}

inline const StaticWorld& staticWorld() {
  static const StaticWorld w = StaticWorld::load(pathFromUtf8(RPG_TEST_SIM_DIR), gameData().balance.collision);
  return w;
}

}  // namespace rpg::test
