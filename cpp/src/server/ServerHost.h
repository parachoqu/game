#pragma once
// Host da simulação: laço de passo fixo, sessões, protocolo e bots. Roda numa thread própria no jogo
// local e no main do servidor dedicado.
//
// A cada passo:
//   1. lê a rede: Hello cria o personagem da sessão; Requests vão para World::handleRequest;
//      PlayerCommand entra na fila da sessão, depois do CommandValidator (taxa e limites);
//   2. aplica um comando da fila de cada sessão (sem comando novo, vale o último: é o passo que a
//      predição do cliente refaz) e roda World::step;
//   3. distribui os eventos pelo destinatário (EventRouter) e manda um snapshot por sessão, com as
//      entidades no raio de interesse do jogador dela, em delta contra o último que o cliente
//      confirmou (SnapshotDelta).
//
// Com `saveDir`, o mundo salvo é carregado ao criar o host; cada personagem é gravado a cada
// `autosaveEvery` s, ao sair (ReqLeave), ao desconectar e no fim de run(). Hello com `resume` retoma o
// personagem salvo com aquele nome.
#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/FixedTimestep.h"
#include "core/Rng.h"
#include "core/data/GameData.h"
#include "core/protocol/Session.h"
#include "core/protocol/SnapshotDelta.h"
#include "net/Transport.h"
#include "server/CommandValidator.h"
#include "sim/World.h"

namespace rpg::server {

namespace persist {
class SaveStore;
}

struct ServerConfig {
  double tickRate = 30.0;       // passos por segundo (dt = 1/30, o mesmo dos testes da demo)
  std::uint64_t seed = 1;       // semente do mundo
  bool allowPause = false;      // só o jogo local de um jogador pode pausar a simulação
  bool devCommands = false;     // aceita comandos de desenvolvimento (tp, give, portal…)
  int snapshotEvery = 1;        // um snapshot a cada N passos
  double interestRadius = 160;  // raio das entidades enviadas a cada cliente (m)
  std::string dataHash;         // resumo dos dados de design; vazio = não confere
  int bots = 0;                 // jogadores controlados pelo servidor (testes de carga)
  // Persistência (game/save.js): diretório dos registros; vazio = nada é salvo nem carregado.
  std::optional<std::filesystem::path> saveDir;
  double autosaveEvery = 20;    // s de jogo entre saves (main.js: a cada 20 s e ao encerrar)
};

struct Session {
  net::ConnectionId connection;
  Tick connectedAt = 0;
  sim::EntityId player = 0;  // 0 até o Hello
  std::uint32_t lastCommandSeq = 0;  // último comando aplicado
  std::uint32_t lastQueuedSeq = 0;   // último comando aceito na fila
  std::string name;
  std::deque<protocol::PlayerCommand> commands;  // um por passo
  std::uint32_t ackSnapshot = 0;                 // base dos deltas
  protocol::DeltaEncoder delta;
  CommandValidator validator;
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

  // Laço de tempo real na thread chamadora, até `stop` virar true. Salva tudo ao sair.
  void run(const std::atomic<bool>& stop);

  // Grava o mundo e cada personagem em jogo (fora da Turbulenta). Sem `saveDir`, não faz nada.
  void saveAll();
  const persist::SaveStore* store() const { return store_.get(); }

  const sim::World& world() const { return world_; }
  sim::World& world() { return world_; }
  const std::vector<Session>& sessions() const { return sessions_; }
  std::uint64_t ignoredMessages() const { return ignoredMessages_; }
  // Comandos e pedidos descartados pelo CommandValidator (taxa).
  std::uint64_t droppedMessages() const { return droppedMessages_; }
  std::size_t botCount() const { return bots_.size(); }

 private:
  struct Bot;

  void tickOnce();
  void pumpNetwork();
  void applyCommands();
  void resetSession(Session& s);
  void onClientMessage(Session& s, protocol::ClientMessage&& msg);
  void routeEvents();
  void sendSnapshots();
  void stepBots();
  void send(net::ConnectionId to, net::Channel ch, const protocol::Bytes& bytes);
  Session* sessionOf(net::ConnectionId id);
  void join(Session& s, const protocol::Hello& hello);
  void leave(Session& s, bool discard);
  void saveCharacter(const Session& s);

  ServerConfig config_;
  std::unique_ptr<net::ITransport> transport_;
  sim::World world_;
  FixedTimestep timestep_;
  std::vector<Session> sessions_;
  std::vector<std::unique_ptr<Bot>> bots_;
  std::unique_ptr<persist::SaveStore> store_;
  double autosaveT_ = 0;
  double clock_ = 0;  // s desde a criação (relógio do validador; anda mesmo com a simulação pausada)
  std::uint64_t ignoredMessages_ = 0, droppedMessages_ = 0;
};

}  // namespace rpg::server
