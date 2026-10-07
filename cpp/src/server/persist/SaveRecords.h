#pragma once
// Registros salvos (game/save.js): o personagem e o mundo, em JSON.
//
// A demo guardava tudo num único objeto no localStorage. Com vários jogadores no mesmo mundo, o save
// se divide em dois:
//
//   personagem — perfil, posição, moedas, treinos, bolsa, alforjes, armazém, equipamento, bolsas no
//                estábulo, Livro, estatísticas, marcas, estrelas observadas e a parte dele no evento;
//   mundo      — hora, evento regional, acampamento, céu, demanda e preço-base dos mercados.
//
// O formato do personagem mantém os nomes do save da demo (`world`, `profile`, `P`, `book`…), de modo
// que a migração e a importação usam o mesmo caminho. Itens e treinos vão pela chave (nunca pelo
// índice), e os equipamentos ganham uid novo ao carregar: o uid é do mundo, não do personagem.
//
// A migração é a da demo: posição de outro layout, inválida ou ausente volta à âncora segura (o
// abrigo do Vale) com uma nota para a interface avisar. Nada é reiniciado em silêncio.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/Math.h"
#include "core/data/GameData.h"
#include "core/world/StaticWorld.h"
#include "sim/World.h"

namespace rpg::server::persist {

using Json = nlohmann::ordered_json;

inline constexpr int kCharacterVersion = 3;  // a demo parou no 2; o 3 é o deste projeto
inline constexpr int kWorldVersion = 1;

// O que a migração mudou (save.js `migrated`).
struct Migration {
  int fromVersion = 0, fromLayout = 0, toLayout = 0;
  bool noPosition = false, layoutChanged = false, invalidPosition = false;
  bool repositioned = false;  // voltou à âncora segura
  std::vector<std::string> notes;  // as notas da demo, para o log
};

// Âncora segura de retorno (save.js safeSpawn): o abrigo do Vale, que existe em todos os layouts.
XZ safeSpawn(const StaticWorld& w);
// Dentro do mundo, fora da Turbulenta, longe da borda, fora de colisor e fora da água (save.js).
bool isValidPosition(const StaticWorld& w, double x, double z);
// save.js saveGame: não se salva dentro da Região Turbulenta (vale o save de antes da entrada).
bool canSave(const sim::Player& p);

Json exportCharacter(const sim::Sim& s, const sim::Player& p);
Json exportWorld(const sim::Sim& s);

// Normaliza um registro de personagem para o formato atual (save.js migrateSave). Devolve nada se o
// objeto não parece um registro de personagem.
std::optional<Json> migrateCharacter(const StaticWorld& w, Json rec, Migration* out = nullptr);

// Perfil do registro, com origem e começo conferidos contra os dados (desconhecidos viram os padrões).
sim::PlayerProfile profileOf(const GameData& data, const Json& rec);

// Cria o personagem a partir de um registro já migrado (save.js applySave, a parte do jogador).
// Itens com chave desconhecida (dados mudaram) são descartados e contados em `dropped`.
sim::EntityId loadCharacter(sim::World& w, const Json& rec, std::uint32_t session, int* dropped = nullptr);
// A parte do mundo de applySave: hora, evento (com os efeitos da reabertura), acampamento, céu e mercados.
void loadWorld(sim::World& w, const Json& rec);

// Save da demo (localStorage "projeto-game-demo-v1") → registros deste projeto. As entradas e os
// títulos do Livro chegam com o texto já escrito (`literal`).
struct ImportedSave {
  Json character, world;
};
std::optional<ImportedSave> importDemoSave(const Json& demo);

}  // namespace rpg::server::persist
