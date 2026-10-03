// O jogo local de hoje: o servidor numa thread e o cliente na principal, ligados por LocalTransport
// (bytes em filas, exatamente como numa rede). É o único executável que linka cliente e servidor.
//
//   rpg_local [--data DIR] [--sim DIR] [--assets DIR] [--size WxH] [--fullscreen] [--no-vsync]
//             [--seed S] [--dev] [--net-sim latency:MS,jitter:MS,loss:PCT]
//             [--auto] [--name N] [--origin O] [--start S] [--model M]
//             [--view NOME] [--hour H] [--hide-hud] [--portrait] [--frames N] [--screenshot ARQ.png] [--headless]
//
//   --auto         pula título e criação (personagem padrão)
//   --view NOME    enquadramento das capturas da demo (02_mercado … 13_vista_elevada_horizonte,
//                  01_title_screen); liga --auto, --dev e esconde o HUD
//   --frames N     sai depois de N quadros de jogo (com --screenshot, grava o último)
//   --portrait     câmera de frente para o próprio personagem (conferir modelo e animação)
//   --headless     sem janela: desenha fora da tela (captura e testes em CI)
#include <atomic>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "client/app/ClientApp.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/data/GameData.h"
#include "core/world/StaticWorld.h"
#include "net/LocalTransport.h"
#include "server/ServerHost.h"

#ifndef RPG_DEFAULT_DATA_DIR
#define RPG_DEFAULT_DATA_DIR "data"
#endif
#ifndef RPG_DEFAULT_SIM_DIR
#define RPG_DEFAULT_SIM_DIR "assets/sim"
#endif
#ifndef RPG_DEFAULT_ASSETS_DIR
#define RPG_DEFAULT_ASSETS_DIR "assets"
#endif

namespace {

template <class T>
bool parseNumber(std::string_view s, T& out) {
  const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
  return ec == std::errc{} && p == s.data() + s.size();
}

// "latency:120,jitter:30,loss:2"
bool parseNetSim(std::string_view s, rpg::net::NetSim& out) {
  while (!s.empty()) {
    const std::size_t comma = s.find(',');
    const std::string_view part = s.substr(0, comma);
    const std::size_t colon = part.find(':');
    if (colon == std::string_view::npos) return false;
    const std::string_view key = part.substr(0, colon);
    double v = 0;
    if (!parseNumber(part.substr(colon + 1), v)) return false;
    if (key == "latency") out.latencyMs = v;
    else if (key == "jitter") out.jitterMs = v;
    else if (key == "loss") out.lossPercent = v;
    else return false;
    s = comma == std::string_view::npos ? std::string_view{} : s.substr(comma + 1);
  }
  return true;
}

int usage() {
  std::fprintf(stderr,
               "uso: rpg_local [--data DIR] [--sim DIR] [--assets DIR] [--size WxH] [--fullscreen] [--no-vsync] [--seed S]\n"
               "                [--dev] [--net-sim latency:MS,jitter:MS,loss:PCT] [--auto] [--name N] [--origin O]\n"
               "                [--start S] [--model M] [--view NOME] [--hour H] [--hide-hud] [--portrait] [--frames N]\n"
               "                [--screenshot ARQ.png] [--headless]\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path dataDir = rpg::pathFromUtf8(RPG_DEFAULT_DATA_DIR);
  std::filesystem::path simDir = rpg::pathFromUtf8(RPG_DEFAULT_SIM_DIR);
  rpg::client::AppOptions opt;
  opt.assetsDir = rpg::pathFromUtf8(RPG_DEFAULT_ASSETS_DIR);
  rpg::server::ServerConfig cfg;
  cfg.allowPause = true;
  rpg::net::NetSim netSim;

  for (int i = 1; i < argc; ++i) {
    const std::string_view a = argv[i];
    const bool v = i + 1 < argc;
    if (a == "--data" && v) dataDir = rpg::pathFromUtf8(argv[++i]);
    else if (a == "--sim" && v) simDir = rpg::pathFromUtf8(argv[++i]);
    else if (a == "--assets" && v) opt.assetsDir = rpg::pathFromUtf8(argv[++i]);
    else if (a == "--size" && v) {
      const std::string_view s = argv[++i];
      const std::size_t x = s.find('x');
      if (x == std::string_view::npos || !parseNumber(s.substr(0, x), opt.width) || !parseNumber(s.substr(x + 1), opt.height)) return usage();
    } else if (a == "--fullscreen") opt.fullscreen = true;
    else if (a == "--no-vsync") opt.vsync = false;
    else if (a == "--seed" && v && parseNumber(std::string_view(argv[++i]), cfg.seed)) {}
    else if (a == "--dev") opt.dev = cfg.devCommands = true;
    else if (a == "--net-sim" && v) {
      if (!parseNetSim(argv[++i], netSim)) return usage();
    } else if (a == "--auto") opt.autoStart = true;
    else if (a == "--name" && v) opt.name = argv[++i];
    else if (a == "--origin" && v) opt.origin = argv[++i];
    else if (a == "--start" && v) opt.start = argv[++i];
    else if (a == "--model" && v) opt.model = argv[++i];
    else if (a == "--view" && v) opt.view = argv[++i];
    else if (a == "--hour" && v) {
      double h = 0;
      if (!parseNumber(std::string_view(argv[++i]), h)) return usage();
      opt.hour = h;
    } else if (a == "--hide-hud") opt.hideHud = true;
    else if (a == "--portrait") opt.portrait = true;
    else if (a == "--frames" && v && parseNumber(std::string_view(argv[++i]), opt.frames)) {}
    else if (a == "--screenshot" && v) opt.screenshot = rpg::pathFromUtf8(argv[++i]);
    else if (a == "--headless") opt.headless = true;
    else return usage();
  }
  if (opt.view) {
    if (!rpg::client::ClientApp::knownView(*opt.view)) {
      rpg::log::error("enquadramento desconhecido: {}", *opt.view);
      return 2;
    }
    if (*opt.view != "01_title_screen" && *opt.view != "01b_create_screen") {
      opt.autoStart = true;
      opt.dev = cfg.devCommands = true;
      // 14–17: o HUD e o menu (intenções, inventário, Livro, mapa) por cima do jogo
      opt.hideHud = !(opt.view->starts_with("14_") || opt.view->starts_with("15_") || opt.view->starts_with("16_") || opt.view->starts_with("17_"));
      if (!opt.hour) opt.hour = 12.0;  // capture-demo-views.mjs: G.time = 52,5 (meio-dia)
    }
    if (opt.frames <= 0) opt.frames = 90;
    opt.fixedDt = 1.0 / 30.0;
  }
  opt.dataDir = dataDir;

  rpg::GameData data;
  std::optional<rpg::StaticWorld> statics;
  try {
    data = rpg::GameData::load(dataDir);
    statics.emplace(rpg::StaticWorld::load(simDir, data.balance.collision));
  } catch (const std::exception& e) {
    rpg::log::error("falha ao carregar o mundo: {}", e.what());
    return 1;
  }

  rpg::net::LocalHub hub(netSim);
  auto serverEnd = hub.serverEndpoint();
  auto clientEnd = hub.connectClient();
  std::atomic<bool> stop{false};
  rpg::server::ServerHost host(data, *statics, cfg, std::move(serverEnd));
  std::thread serverThread([&] { host.run(stop); });

  int code = 0;
  try {
    rpg::client::ClientApp app(data, *statics, opt, std::move(clientEnd));
    code = app.run();
  } catch (const std::exception& e) {
    rpg::log::error("cliente: {}", e.what());
    code = 1;
  }
  stop.store(true);
  serverThread.join();
  return code;
}
