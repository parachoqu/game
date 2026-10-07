// Servidor dedicado, sem janela: carrega os dados de design e o mundo, roda a simulação em passo fixo
// e, com --listen, recebe jogadores pela rede (ENet).
//
//   rpg_server [--data DIR] [--sim DIR] [--ticks N] [--seed S] [--tick-rate HZ] [--listen [PORTA]]
//
//   --listen [PORTA]  aceita jogadores pela rede (UDP, porta padrão 27450): rpg_local --connect HOST:PORTA
//   --data DIR  dados de design (cpp/data)
//   --sim DIR   pacote de mundo da simulação (cpp/assets/sim)
//   --ticks N   roda N passos o mais rápido possível e sai (CI e testes de fumaça)
//   sem --ticks roda em tempo real até Ctrl+C
//   --save-dir DIR  mundo e personagens salvos (padrão: <diretório do usuário>/servidor; com --ticks, nenhum)
//   --no-save       não carrega nem grava nada
#include <atomic>
#include <charconv>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

#include "core/Log.h"
#include "core/Paths.h"
#include "core/data/DataHash.h"
#include "core/data/GameData.h"
#include "core/world/StaticWorld.h"
#include "net/EnetTransport.h"
#include "server/ServerHost.h"

#ifndef RPG_DEFAULT_DATA_DIR
#define RPG_DEFAULT_DATA_DIR "data"
#endif
#ifndef RPG_DEFAULT_SIM_DIR
#define RPG_DEFAULT_SIM_DIR "assets/sim"
#endif

namespace {

std::atomic<bool> gStop{false};
void onSignal(int) { gStop.store(true); }

template <class T>
bool parseNumber(std::string_view s, T& out) {
  const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
  return ec == std::errc{} && p == s.data() + s.size();
}

int usage() {
  std::fprintf(stderr,
               "uso: rpg_server [--data DIR] [--sim DIR] [--ticks N] [--seed S] [--tick-rate HZ] [--bots N] [--dev]\n"
               "                 [--save-dir DIR] [--no-save] [--listen [PORTA]] [--max-players N]\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path dataDir = rpg::pathFromUtf8(RPG_DEFAULT_DATA_DIR);
  std::filesystem::path simDir = rpg::pathFromUtf8(RPG_DEFAULT_SIM_DIR);
  std::uint64_t ticks = 0;
  rpg::server::ServerConfig cfg;
  std::optional<std::filesystem::path> saveDir;
  bool noSave = false;
  std::optional<std::uint16_t> listen;
  std::size_t maxPlayers = 64;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    const bool hasValue = i + 1 < argc;
    if (arg == "--data" && hasValue) dataDir = rpg::pathFromUtf8(argv[++i]);
    else if (arg == "--sim" && hasValue) simDir = rpg::pathFromUtf8(argv[++i]);
    else if (arg == "--ticks" && hasValue && parseNumber(argv[++i], ticks)) {}
    else if (arg == "--seed" && hasValue && parseNumber(argv[++i], cfg.seed)) {}
    else if (arg == "--tick-rate" && hasValue && parseNumber(argv[++i], cfg.tickRate) && cfg.tickRate > 0) {}
    else if (arg == "--bots" && hasValue && parseNumber(argv[++i], cfg.bots) && cfg.bots >= 0) {}
    else if (arg == "--dev") cfg.devCommands = true;
    else if (arg == "--save-dir" && hasValue) saveDir = rpg::pathFromUtf8(argv[++i]);
    else if (arg == "--no-save") noSave = true;
    else if (arg == "--listen") {
      std::uint16_t port = rpg::net::kDefaultPort;
      if (hasValue && argv[i + 1][0] != '-') {
        if (!parseNumber(argv[++i], port)) return usage();
      }
      listen = port;
    } else if (arg == "--max-players" && hasValue && parseNumber(argv[++i], maxPlayers) && maxPlayers > 0) {}
    else return usage();
  }
  if (!noSave && (saveDir || ticks == 0)) cfg.saveDir = saveDir ? *saveDir : rpg::userDataDir() / "servidor";

  rpg::GameData data;
  try {
    data = rpg::GameData::load(dataDir);
  } catch (const std::exception& e) {
    rpg::log::error("dados de design inválidos: {}", e.what());
    return 1;
  }
  rpg::log::info("dados carregados de {}: {} itens, {} armas, {} técnicas, {} inimigos, {} receitas",
                 rpg::pathToUtf8(dataDir), data.items.size(), data.weapons.size(), data.skills.size(),
                 data.enemies.size(), data.recipes.size());

  std::optional<rpg::StaticWorld> statics;
  try {
    statics.emplace(rpg::StaticWorld::load(simDir, data.balance.collision));
  } catch (const std::exception& e) {
    rpg::log::error("pacote de mundo inválido: {}", e.what());
    return 1;
  }
  const auto& region = statics->map(rpg::MapKind::Region);
  const auto& turb = statics->map(rpg::MapKind::Turbulent);
  rpg::log::info("mundo (layout {}) de {}: {} colisores na região, {} na Turbulenta, {} pontes",
                 statics->worldLayoutVersion(), rpg::pathToUtf8(simDir), region.collision().size(), turb.collision().size(),
                 region.terrain().bridges().size());

  const auto& layout = statics->layout();
  rpg::log::info("camada de jogo: {} interagíveis, {} pontos de coleta, {} NPCs, {} grupos de inimigos",
                 layout.interactables.size(), layout.nodes.size(), layout.npcs.size(), layout.groups.size());

  // o Hello de cada cliente leva o resumo dos dados: com outros dados, o servidor recusa a entrada
  cfg.dataHash = rpg::dataHash(dataDir);
  std::unique_ptr<rpg::net::ITransport> transport;
  if (listen) {
    try {
      auto server = std::make_unique<rpg::net::EnetServer>(*listen, maxPlayers);
      rpg::log::info("escutando na porta UDP {} (até {} jogadores); dados {}", server->port(), maxPlayers, cfg.dataHash.substr(0, 12));
      transport = std::move(server);
    } catch (const std::exception& e) {
      rpg::log::error("{}", e.what());
      return 1;
    }
  }
  rpg::server::ServerHost host(data, *statics, cfg, std::move(transport));
  const auto& S = host.world().state();
  rpg::log::info("mundo povoado: {} inimigos, {} NPCs, {} bots", S.enemies.size(), S.npcs.size(), host.botCount());
  if (cfg.saveDir) rpg::log::info("saves em {}", rpg::pathToUtf8(*cfg.saveDir));
  if (ticks > 0) {
    host.runTicks(ticks);
    host.saveAll();
  } else {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    rpg::log::info("simulação a {} Hz; Ctrl+C encerra", cfg.tickRate);
    host.run(gStop);
  }
  const rpg::ClockTime c = host.world().clock();
  rpg::log::info("encerrado no tick {} (tempo de jogo {:.2f} s, dia {} às {:.2f} h)", host.world().tick(),
                 host.world().time(), c.day, c.hour);
  int alive = 0;
  for (const auto& e : S.enemies) alive += e->alive ? 1 : 0;
  rpg::log::info("estado final: {} inimigos vivos, acampamento no estado {}, evento em {:.1f}%, {} estrelas sumidas",
                 alive, static_cast<int>(S.camp.state), S.evt.progress, S.skyState.vanished.size());
  return 0;
}
