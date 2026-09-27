#pragma once
// Host da simulação: laço de passo fixo, conexões e (nas próximas fases) comandos, snapshots,
// eventos e persistência. Roda numa thread própria no jogo local e no main do servidor dedicado.
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/FixedTimestep.h"
#include "core/data/GameData.h"
#include "net/Transport.h"
#include "sim/World.h"

namespace rpg::server {

struct ServerConfig {
  double tickRate = 30.0;     // passos por segundo (dt = 1/30, o mesmo dos testes da demo)
  std::uint64_t seed = 1;     // semente do mundo
  bool allowPause = false;    // só o jogo local de um jogador pode pausar a simulação
};

struct Session {
  net::ConnectionId connection;
  Tick connectedAt = 0;
};

class ServerHost {
 public:
  // `transport` pode ser nulo (servidor sem clientes, para testes e bancadas).
  ServerHost(const GameData& data, const StaticWorld& statics, ServerConfig config,
             std::unique_ptr<net::ITransport> transport);

  // Avança `n` passos já, sem esperar o relógio (testes e execução headless acelerada).
  void runTicks(std::uint64_t n);

  // Uma volta do laço de tempo real: lê a rede e roda os passos fixos que couberem em `elapsed`.
  // Devolve quantos passos rodaram.
  int update(Seconds elapsed);

  // Laço de tempo real na thread chamadora, até `stop` virar true.
  void run(const std::atomic<bool>& stop);

  const sim::World& world() const { return world_; }
  const std::vector<Session>& sessions() const { return sessions_; }
  std::uint64_t ignoredMessages() const { return ignoredMessages_; }

 private:
  void pumpNetwork();

  ServerConfig config_;
  std::unique_ptr<net::ITransport> transport_;
  sim::World world_;
  FixedTimestep timestep_;
  std::vector<Session> sessions_;
  std::uint64_t ignoredMessages_ = 0;
};

}  // namespace rpg::server
