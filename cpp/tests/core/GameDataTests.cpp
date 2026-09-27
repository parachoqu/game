#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include "TestData.h"
#include "core/data/GameData.h"

namespace fs = std::filesystem;
using Catch::Matchers::ContainsSubstring;

namespace {

const fs::path kData = RPG_TEST_DATA_DIR;

const rpg::GameData& data() { return rpg::test::gameData(); }

nlohmann::json readJson(const fs::path& p) {
  std::ifstream in(p);
  return nlohmann::json::parse(in);
}

// Cópia de cpp/data num diretório temporário, para testar dados quebrados.
struct TempData {
  fs::path dir;
  TempData() {
    dir = fs::temp_directory_path() / ("rpg-data-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    fs::remove_all(dir);
    fs::copy(kData, dir, fs::copy_options::recursive);
  }
  ~TempData() { fs::remove_all(dir); }
  void write(const std::string& name, const std::string& body) const { std::ofstream(dir / name) << body; }
};

}  // namespace

TEST_CASE("GameData carrega o config.js exportado inteiro", "[core][data]") {
  const auto& d = data();
  CHECK(d.items.size() == 32);
  CHECK(d.weapons.size() == 19);
  CHECK(d.skills.size() == 36);
  CHECK(d.trainings.size() == 11);
  CHECK(d.enemies.size() == 10);  // 9 de ENEMIES + o guarda
  CHECK(d.zones.size() == 4);
  CHECK(d.markets.size() == 2);
  CHECK(d.recipes.size() == 24);
  CHECK(d.origins.size() == 4);
  CHECK(d.starts.size() == 8);
  CHECK(d.mountPrice == 80);
  CHECK(d.restartKitPrice == 8);
}

TEST_CASE("GameData: valores conferidos com o config.js", "[core][data]") {
  const auto& d = data();

  const auto espada = d.items.find("espada");
  REQUIRE(espada);
  CHECK(d.items[espada].kind == rpg::ItemKind::Weapon);
  CHECK(d.items[espada].weight == 3);
  CHECK(d.trainings.key(d.items[espada].requiredTraining) == "espadachim");
  const auto& fam = d.weapons[d.items[espada].family];
  CHECK(fam.basic.type == rpg::AttackType::Melee);
  CHECK(fam.basic.damage == 14);
  CHECK(fam.basic.range == 2.8);
  REQUIRE(fam.skills.size() == 2);
  CHECK(d.skills.key(fam.skills[0]) == "investida");
  CHECK(d.skills[fam.skills[1]].cooldown == 9);

  const auto cota = d.items.find("cota");
  CHECK(d.items[cota].armorHp == 45);
  CHECK(d.items[cota].armorSpeed == 0.93);
  CHECK(d.items[d.items.find("bolsa")].capacity == 15);
  CHECK(d.items[d.items.find("minerio")].isGear() == false);
  CHECK(d.items[d.items.find("montaria")].isGear());

  const auto& gelo = d.weapons[d.weapons.find("cajado_gelo")].basic;
  CHECK(gelo.type == rpg::AttackType::Spell);
  CHECK(gelo.school == rpg::SpellSchool::Ice);
  CHECK(gelo.speed == 38);
  CHECK(d.weapons[d.weapons.find("punhos")].skills.empty());

  const auto& lider = d.enemies[d.enemies.find("garraLider")];
  CHECK(lider.shape == rpg::AttackShape::Circle);
  CHECK(lider.scale == 1.45);
  REQUIRE(lider.drops.size() == 1);
  CHECK(d.items.key(lider.drops[0].item) == "pele");
  CHECK(lider.drops[0].min == 2);
  CHECK(lider.drops[0].max == 3);
  const auto& arqueiro = d.enemies[d.enemies.find("saqueadorArco")];
  CHECK(arqueiro.ranged);
  CHECK(arqueiro.arc == 1.4);  // ausente no config.js: o JS usa `cfg.arc || 1.4`
  CHECK(arqueiro.coins == std::pair{4, 8});
  const auto& guarda = d.enemies[d.enemies.find("guarda")];
  CHECK(guarda.faction == rpg::Faction::Guard);
  CHECK(guarda.hp == 200);

  CHECK(d.zones[d.zones.find("protegida")].loss == rpg::LossRule::Wear);
  CHECK(d.zones[d.zones.find("fronteira")].loss == rpg::LossRule::Cargo);
  CHECK(d.zones[d.zones.find("turbulenta")].loss == rpg::LossRule::Full);

  const auto& vale = d.markets[d.markets.find("vale")];
  CHECK(vale.buyPrice(d.items.find("espada")) == 42.0);
  CHECK(vale.sellBase(d.items.find("madeira")) == 1.5);
  CHECK_FALSE(vale.buyPrice(d.items.find("cristal")).has_value());

  const auto& pocao = d.recipes[d.recipes.find("pocao")];
  CHECK(pocao.quantity == 2);
  CHECK(d.trainings.key(pocao.requiredTraining) == "alquimista");
  REQUIRE(pocao.inputs.size() == 1);
  CHECK(pocao.inputs[0].second == 3);
  CHECK_FALSE(d.recipes[d.recipes.find("lingote")].requiredTraining.valid());

  // arquétipos: a chave vira treino só quando existe em TRAININGS
  CHECK(d.starts[d.starts.find("espadachim")].training == d.trainings.find("espadachim"));
  CHECK_FALSE(d.starts[d.starts.find("lanceiro")].training.valid());
  CHECK(d.items.key(d.starts[d.starts.find("lanceiro")].weapon) == "lanca");

  CHECK(d.balance.clock.dayLength == 420);
  CHECK(d.balance.movement.run == 7.2);
  CHECK(d.balance.movement.boost.speed == 9.0);
  CHECK(d.balance.collision.maxSlope == 1.25);
  CHECK(d.balance.inventory.overLimit == 1.3);
  CHECK(d.balance.turbulent.duration == 240);
}

TEST_CASE("GameData preserva a ordem de declaração do config.js", "[core][data]") {
  const auto& d = data();
  CHECK(d.items.key(rpg::ItemId{0}) == "minerio");
  CHECK(d.items.key(rpg::ItemId{static_cast<std::uint16_t>(d.items.size() - 1)}) == "tomo_sagrado");
  CHECK(d.zones.key(rpg::ZoneId{0}) == "protegida");
  CHECK(d.enemies.key(rpg::EnemyTypeId{static_cast<std::uint16_t>(d.enemies.size() - 1)}) == "guarda");
}

TEST_CASE("Todo id de regra tem texto pt-BR (e aparência, quando aplicável)", "[core][data]") {
  const auto& d = data();
  const fs::path text = kData / "text" / "pt-BR";
  const auto items = readJson(text / "items.json");
  const auto look = readJson(kData / "presentation" / "items.json");
  for (auto id : d.items.ids()) {
    const auto& key = d.items.key(id);
    INFO(key);
    CHECK(items.at(key).at("name").is_string());
    CHECK(look.at(key).at("glyph").is_string());
  }
  const auto skills = readJson(text / "skills.json");
  for (auto id : d.skills.ids()) CHECK(skills.at(d.skills.key(id)).contains("name"));
  const auto enemies = readJson(text / "enemies.json");
  for (auto id : d.enemies.ids()) CHECK(enemies.at(d.enemies.key(id)).contains("name"));
  const auto zones = readJson(text / "zones.json");
  for (auto id : d.zones.ids()) CHECK(zones.at(d.zones.key(id)).contains("rule"));
  const auto recipes = readJson(text / "recipes.json");
  for (auto id : d.recipes.ids()) CHECK(recipes.at(d.recipes.key(id)).contains("note"));
}

TEST_CASE("GameData recusa dados quebrados com arquivo e chave na mensagem", "[core][data]") {
  SECTION("referência a item inexistente") {
    TempData t;
    t.write("recipes.json", R"([{"out":"espada","qty":1,"in":{"unobtainium":2},"req":null}])");
    CHECK_THROWS_WITH(rpg::GameData::load(t.dir), ContainsSubstring("unobtainium") && ContainsSubstring("recipes.json"));
  }
  SECTION("técnica desconhecida numa arma") {
    TempData t;
    t.write("weapons.json", R"({"punhos":{"basic":{"type":"melee","dmg":6,"range":1.9,"arc":1.7,"windup":0.08,"recover":0.28},"skills":["voar"]}})");
    CHECK_THROWS_WITH(rpg::GameData::load(t.dir), ContainsSubstring("voar"));
  }
  SECTION("campo obrigatório ausente") {
    TempData t;
    t.write("skills.json", R"({"investida":{"cost":30}})");
    CHECK_THROWS_WITH(rpg::GameData::load(t.dir), ContainsSubstring("skills.json › investida › cd"));
  }
  SECTION("JSON inválido") {
    TempData t;
    t.write("zones.json", "{ isto não é json");
    CHECK_THROWS_WITH(rpg::GameData::load(t.dir), ContainsSubstring("zones.json"));
  }
}
