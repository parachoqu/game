#pragma once
// Sessão do cliente: entrada no jogo (Hello → Welcome/Reject), envio de comandos e pedidos,
// recepção de snapshots e eventos. Só conhece bytes pelo ITransport: o mesmo código serve ao jogo
// local (LocalTransport) e ao online (ENet).
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "client/net/SnapshotInterpolator.h"
#include "core/protocol/PlayerCommand.h"
#include "core/protocol/Session.h"
#include "net/Transport.h"

namespace rpg::client {

class ClientSession {
 public:
  enum class State : std::uint8_t { Idle, Joining, InGame, Rejected, Disconnected };

  explicit ClientSession(std::unique_ptr<net::ITransport> transport);

  void join(const protocol::Hello& hello);
  void send(const protocol::Request& req);
  void send(const protocol::PlayerCommand& cmd);

  // Lê tudo o que chegou. Snapshots vão para o interpolador; eventos para a fila.
  void poll(double localTime);
  std::vector<protocol::GameEvent> takeEvents();

  State state() const { return state_; }
  const std::string& rejectReason() const { return reject_; }
  const protocol::Welcome& welcome() const { return welcome_; }
  std::uint32_t playerId() const { return welcome_.playerId; }

  SnapshotInterpolator& snapshots() { return snapshots_; }
  const SnapshotInterpolator& snapshots() const { return snapshots_; }

  std::uint64_t bytesIn() const { return bytesIn_; }
  std::uint64_t bytesOut() const { return bytesOut_; }
  std::uint64_t badMessages() const { return bad_; }

 private:
  void sendBytes(net::Channel ch, const protocol::Bytes& b);

  std::unique_ptr<net::ITransport> transport_;
  State state_ = State::Idle;
  std::string reject_;
  protocol::Welcome welcome_;
  SnapshotInterpolator snapshots_;
  std::vector<protocol::GameEvent> events_;
  std::uint64_t bytesIn_ = 0, bytesOut_ = 0, bad_ = 0;
};

}  // namespace rpg::client
