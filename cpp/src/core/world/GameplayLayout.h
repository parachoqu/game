#pragma once
// Camada de jogo sobre o mundo estático (game/world.js `buildWorld`, layout.js, turbulent.js,
// discovery.js, npcs.js): onde ficam serviços, pontos de coleta, NPCs, grupos de inimigos, postos de
// guarda, o acampamento, os pontos de portal e os pontos da Turbulenta.
//
// Nada disso é digitado no C++: `demo/tools/bake-sim-world.mjs` roda o `buildWorld` da demo e grava
// cpp/assets/sim/gameplay-layout.json na ordem em que a demo criou cada coisa (a ordem decide empates
// em "o alvo mais próximo" e a sequência de sorteios na criação dos inimigos). Textos (rótulos,
// biografias dos viajantes) ficam em data/text; aparência (raça, roupa) serve só ao cliente.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/Types.h"

namespace rpg {

// Interagível fixo (G.interactables): serviço ou ponto de coleta.
struct LayoutInteractable {
  std::string kind;   // market, forge, trainer, storage, stable, board, canteiro, vigia, observatory, astronomer, node
  double x = 0, z = 0, r = 0;
  std::string data;   // mercado ("vale"/"alto"); vazio nos demais
  int node = -1;      // índice em `nodes` quando kind == "node"
  std::string label;  // chave do rótulo em data/text/pt-BR/interactables.json
};

struct LayoutNode {
  std::string kind;  // minerio, madeira, erva, cristal
  double x = 0, z = 0;
  int yieldBonus = 0;
  double rot = 0;  // rotação do modelo (só aparência)
};

struct LayoutNpc {
  double x = 0, z = 0, yaw = 0;
  std::string role;  // merchant, smith, trainer, stable, guard, foreman, astro, astronomer, traveler
  bool face = false, work = false;
  std::string race, cloth, trim, hood, weapon, model, name;
  int traveler = -1;  // índice em `travelers`
};

struct LayoutTraveler {
  std::string key;               // chave estável (texto em data/text/pt-BR/travelers.json)
  std::vector<XZ> path;
};

struct LayoutGroup {
  std::string id;
  double x = 0, z = 0;
  std::optional<double> leash;
  double respawn = 70;
  bool gorge = false;  // ativo só enquanto a Passagem não reabre (ou 35% depois)
  struct Member {
    std::string type;
    double dx = 0, dz = 0;
  };
  std::vector<Member> members;
};

struct LayoutPost {
  double x = 0, z = 0, yaw = 0;
};

struct LayoutDiscovery {
  std::string key;
  double x = 0, z = 0, r = 0;
  bool byZone = false;  // Ermos: descobre ao entrar em Full Loot
};

struct GameplayLayout {
  std::vector<LayoutInteractable> interactables;
  std::vector<LayoutNode> nodes;
  std::vector<LayoutNpc> npcs;
  std::vector<LayoutTraveler> travelers;
  std::vector<LayoutGroup> groups;
  LayoutPost canteiroGuard;
  std::vector<LayoutPost> guardPosts;  // depois da reabertura da Passagem
  XZ campCenter, campDisplaced;
  std::vector<XZ> campPatrol;
  std::vector<XZ> portalSpots;
  XZ shelter;  // abrigo do Vale (respawn)
  struct Turbulent {
    std::vector<XZ> shrines;  // coordenadas do mapa da Turbulenta
    std::vector<XZ> exits;
    std::vector<XZ> starts;   // entrada (antes da dispersão de ±6 m)
    XZ portalCore;            // centro do portal, relativo ao centro da região
    XZ center;                // TURB.x, TURB.z
  } turbulent;
  std::vector<LayoutDiscovery> discoveries;
  std::vector<XZ> shortGorge;  // rota curta (evento: saqueadores afastados na Passagem)

  static GameplayLayout load(const std::filesystem::path& file);
};

}  // namespace rpg
