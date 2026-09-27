// Servidor dedicado, sem janela. Nesta fase ele carrega e valida os dados de design e roda a
// simulação em passo fixo — o esqueleto que as próximas fases vão preenchendo.
//
//   rpg_server [--data DIR] [--sim DIR] [--ticks N] [--seed S] [--tick-rate HZ]
//
//   --data DIR  dados de design (cpp/data)
//   --sim DIR   pacote de mundo da simulação (cpp/assets/sim)
//   --ticks N   roda N passos o mais rápido possível e sai (CI e testes de fumaça)
//   sem --ticks roda em tempo real até Ctrl+C
#include <atomic>
#include <charconv>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string_view>

#include "core/Log.h"
#include "core/data/GameData.h"
#include "core/world/StaticWorld.h"
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
  std::fprintf(stderr, "uso: rpg_server [--data DIR] [--sim DIR] [--ticks N] [--seed S] [--tick-rate HZ]\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path dataDir = RPG_DEFAULT_DATA_DIR;
  std::filesystem::path simDir = RPG_DEFAULT_SIM_DIR;
  std::uint64_t ticks = 0;
  rpg::server::ServerConfig cfg;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    const bool hasValue = i + 1 < argc;
    if (arg == "--data" && hasValue) dataDir = argv[++i];
    else if (arg == "--sim" && hasValue) simDir = argv[++i];
    else if (arg == "--ticks" && hasValue && parseNumber(argv[++i], ticks)) {}
    else if (arg == "--seed" && hasValue && parseNumber(argv[++i], cfg.seed)) {}
    else if (arg == "--tick-rate" && hasValue && parseNumber(argv[++i], cfg.tickRate) && cfg.tickRate > 0) {}
    else return usage();
  }

  rpg::GameData data;
  try {
    data = rpg::GameData::load(dataDir);
  } catch (const std::exception& e) {
    rpg::log::error("dados de design inválidos: {}", e.what());
    return 1;
  }
  rpg::log::info("dados carregados de {}: {} itens, {} armas, {} técnicas, {} inimigos, {} receitas",
                 dataDir.string(), data.items.size(), data.weapons.size(), data.skills.size(),
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
                 statics->worldLayoutVersion(), simDir.string(), region.collision().size(), turb.collision().size(),
                 region.terrain().bridges().size());

  rpg::server::ServerHost host(data, *statics, cfg, nullptr);
  if (ticks > 0) {
    host.runTicks(ticks);
  } else {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    rpg::log::info("simulação a {} Hz; Ctrl+C encerra", cfg.tickRate);
    host.run(gStop);
  }
  const rpg::ClockTime c = host.world().clock();
  rpg::log::info("encerrado no tick {} (tempo de jogo {:.2f} s, dia {} às {:.2f} h)", host.world().tick(),
                 host.world().time(), c.day, c.hour);
  return 0;
}
