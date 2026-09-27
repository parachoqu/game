#pragma once
// Host da simulação: laço de passo fixo, sessões, protocolo e bots. Roda numa thread própria no jogo
// local e no main do servidor dedicado.
//
// A cada passo:
//   1. lê a rede: Hello cria o personagem da sessão; Requests vão para World::handleRequest;
//      PlayerCommand substitui o comando da sessão (o mais novo vale);
//   2. roda World::step;
//   3. distribui os eventos pelo destinatário (EventRouter) e manda um snapshot por sessão, com as
//      entidades no raio de interesse do jogador dela.
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/FixedTimestep.h"
#include "core/Rng.h"
#include "core/data/GameData.h"
#include "core/protocol/Session.h"
#include "net/Transport.h"
#include "sim/World.h"

namespace rpg::server {

struct ServerConfig {
  double tickRate = 30.0;       // passos por segundo (dt = 1/30, o mesmo dos testes da demo)
  std::uint64_t seed = 1;       // semente do mundo
  bool allowPause = false;      // só o jogo local de um jogador pode pausar a simulação
  bool devCommands = false;     // aceita comandos de desenvolvimento (tp, give, portal…)
  int snapshotEvery = 1;        // um snapshot a cada N passos
  double interestRadius = 160;  // raio das entidades enviadas a cada cliente (m)
  std::string dataHash;         // resumo dos dados de design; vazio = não confere
  int bots = 0;                 // jogadores controlados pelo servidor (testes de carga)
};

struct Session {
  net::ConnectionId connection;
  Tick connectedAt = 0;
  sim::EntityId player = 0;  // 0 até o Hello
  std::uint32_t lastCommandSeq = 0;
  std::string name;
};

class ServerHost {
 public:
  // `transport` pode ser nulo (servidor sem clientes, para testes e bancadas).
  ServerHost(const GameData& data, const StaticWorld& statics, ServerConfig config,
             std::unique_ptr<net::ITransport> transport);
  ~ServerHost();

  // Avança `n` passos já, sem esperar o relógio (testes e execução headless acelerada).
  void runTicks(std::uint64_t n);

  // Uma volta do laço de tempo real: lê a rede e roda os passos fixos que couberem em `elapsed`.
  // Devolve quantos passos rodaram.
  int update(Seconds elapsed);

  // Laço de tempo real na thread chamadora, até `stop` virar true.
  void run(const std::atomic<bool>& stop);

  const sim::World& world() const { return world_; }
  sim::World& world() { return world_; }
  const std::vector<Session>& sessions() const { return sessions_; }
  std::uint64_t ignoredMessages() const { return ignoredMessages_; }
  std::size_t botCount() const { return bots_.size(); }

 private:
  struct Bot;

  void tickOnce();
  void pumpNetwork();
  void onClientMessage(Session& s, protocol::ClientMessage&& msg);
  void routeEvents();
  void sendSnapshots();
  void stepBots();
  void send(net::ConnectionId to, net::Channel ch, const protocol::Bytes& bytes);
  Session* sessionOf(net::ConnectionId id);

  ServerConfig config_;
  std::unique_ptr<net::ITransport> transport_;
  sim::World world_;
  FixedTimestep timestep_;
  std::vector<Session> sessions_;
  std::vector<std::unique_ptr<Bot>> bots_;
  std::uint64_t ignoredMessages_ = 0;
};

}  // namespace rpg::server
