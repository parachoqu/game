#include "server/ServerHost.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include "core/Log.h"

namespace rpg::server {

ServerHost::ServerHost(const GameData& data, const StaticWorld& statics, ServerConfig config,
                       std::unique_ptr<net::ITransport> transport)
    : config_(config),
      transport_(std::move(transport)),
      world_(data, statics, config.seed),
      timestep_(1.0 / config.tickRate) {}

void ServerHost::runTicks(std::uint64_t n) {
  for (std::uint64_t i = 0; i < n; ++i) {
    pumpNetwork();
    world_.step(timestep_.step());
  }
}

int ServerHost::update(Seconds elapsed) {
  pumpNetwork();
  const int steps = timestep_.advance(elapsed);
  for (int i = 0; i < steps; ++i) world_.step(timestep_.step());
  return steps;
}

void ServerHost::run(const std::atomic<bool>& stop) {
  using clock = std::chrono::steady_clock;
  auto last = clock::now();
  while (!stop.load(std::memory_order_relaxed)) {
    const auto now = clock::now();
    update(std::chrono::duration<double>(now - last).count());
    last = now;
    // dorme até o próximo passo previsto; a precisão fina vem do acumulador, não do sono
    const double wait = (1.0 - timestep_.alpha()) * timestep_.step();
    std::this_thread::sleep_for(std::chrono::duration<double>(std::max(0.0005, wait)));
  }
}

void ServerHost::pumpNetwork() {
  if (!transport_) return;
  net::Message msg;
  while (transport_->poll(msg)) {
    switch (msg.kind) {
      case net::Message::Kind::Connected:
        sessions_.push_back({msg.peer, world_.tick()});
        log::info("cliente {} conectado (tick {})", msg.peer.value, world_.tick());
        break;
      case net::Message::Kind::Disconnected:
        std::erase_if(sessions_, [&](const Session& s) { return s.connection == msg.peer; });
        log::info("cliente {} desconectado", msg.peer.value);
        break;
      case net::Message::Kind::Data:
        // O protocolo (PlayerCommand, Request) entra nas fases 1–2; até lá, mensagens são contadas.
        ++ignoredMessages_;
        break;
    }
  }
}

}  // namespace rpg::server
