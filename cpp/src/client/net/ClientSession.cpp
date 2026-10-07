#include "client/net/ClientSession.h"

#include <type_traits>
#include <variant>

#include "net/Bytes.h"

namespace rpg::client {

namespace proto = protocol;

ClientSession::ClientSession(std::unique_ptr<net::ITransport> transport) : transport_(std::move(transport)) {}

void ClientSession::sendBytes(net::Channel ch, const proto::Bytes& b) {
  bytesOut_ += b.size();
  transport_->send(net::kServer, ch, net::asBytes(b));
  transport_->flush();
}

void ClientSession::join(const proto::Hello& hello) {
  state_ = State::Joining;
  sendBytes(net::Channel::Requests, proto::encode(proto::ClientMessage{hello}));
}

void ClientSession::leave(bool discard) {
  if (state_ == State::InGame) sendBytes(net::Channel::Requests, proto::encode(proto::ClientMessage{proto::Request{proto::ReqLeave{discard}}}));
  state_ = State::Idle;
  welcome_ = {};
  snapshots_.clear();
  events_.clear();
}

void ClientSession::send(const proto::Request& req) {
  if (state_ != State::InGame) return;
  sendBytes(net::Channel::Requests, proto::encode(proto::ClientMessage{req}));
}

void ClientSession::send(proto::PlayerCommand cmd) {
  if (state_ != State::InGame) return;
  cmd.ackSnapshot = deltas_.lastSeq();
  sendBytes(net::Channel::Commands, proto::encode(cmd));
}

void ClientSession::poll(double localTime) {
  net::Message msg;
  while (transport_->poll(msg)) {
    if (msg.kind == net::Message::Kind::Disconnected) {
      state_ = State::Disconnected;
      continue;
    }
    if (msg.kind != net::Message::Kind::Data) continue;
    bytesIn_ += msg.bytes.size();
    if (msg.channel == net::Channel::Snapshots) {
      auto delta = proto::decode<proto::SnapshotDelta>(net::asU8(msg.bytes));
      if (!delta) {
        ++bad_;
        continue;
      }
      if (state_ != State::InGame) continue;
      // a numeração do servidor nunca volta (nem entre personagens): o decodificador vale a sessão toda
      auto snap = deltas_.decode(*delta);
      if (!snap) {
        ++stale_;
        continue;
      }
      ++(delta->base == 0 ? full_ : partial_);
      snapshots_.push(std::move(*snap), localTime);
      continue;
    }
    if (msg.channel != net::Channel::Events) continue;
    auto m = proto::decode<proto::ServerMessage>(net::asU8(msg.bytes));
    if (!m) {
      ++bad_;
      continue;
    }
    std::visit(
        [&](auto&& v) {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, proto::Welcome>) {
            welcome_ = v;
            state_ = State::InGame;
            snapshots_.clear();
            snapshots_.setTickRate(v.tickRate);
          } else if constexpr (std::is_same_v<T, proto::Reject>) {
            reject_ = v.reason;
            state_ = State::Rejected;
          } else if (state_ == State::InGame) {
            // o que ainda chega do personagem anterior (depois de leave) é descartado
            events_.push_back(std::move(v));
          }
        },
        std::move(*m));
  }
}

std::vector<proto::GameEvent> ClientSession::takeEvents() {
  std::vector<proto::GameEvent> out;
  out.swap(events_);
  return out;
}

}  // namespace rpg::client
