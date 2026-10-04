#include "net/EnetTransport.h"

#include <charconv>
#include <cstring>
#include <mutex>
#include <stdexcept>

#include <enet/enet.h>

namespace rpg::net {

namespace {

// enet_initialize/deinitialize contados (servidor e clientes no mesmo processo nos testes)
std::mutex gInitMutex;
int gInitCount = 0;

void acquireEnet() {
  std::lock_guard lock(gInitMutex);
  if (gInitCount++ == 0 && enet_initialize() != 0) {
    gInitCount = 0;
    throw std::runtime_error("enet_initialize falhou");
  }
}

void releaseEnet() {
  std::lock_guard lock(gInitMutex);
  if (--gInitCount == 0) enet_deinitialize();
}

constexpr enet_uint32 flagsFor(Channel c) {
  switch (c) {
    case Channel::Requests:
    case Channel::Events: return ENET_PACKET_FLAG_RELIABLE;
    case Channel::Snapshots: return ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT;
    case Channel::Commands:
    case Channel::Count: break;
  }
  return 0;
}

constexpr std::size_t kChannels = static_cast<std::size_t>(Channel::Count);

void sendTo(_ENetPeer* peer, Channel ch, std::span<const std::byte> bytes) {
  ENetPacket* pkt = enet_packet_create(bytes.data(), bytes.size(), flagsFor(ch));
  if (enet_peer_send(peer, static_cast<enet_uint8>(ch), pkt) != 0) enet_packet_destroy(pkt);
}

Message dataMessage(ConnectionId from, const ENetEvent& ev) {
  Message m;
  m.kind = Message::Kind::Data;
  m.peer = from;
  m.channel = ev.channelID < kChannels ? static_cast<Channel>(ev.channelID) : Channel::Count;
  const auto* p = reinterpret_cast<const std::byte*>(ev.packet->data);
  m.bytes.assign(p, p + ev.packet->dataLength);
  return m;
}

}  // namespace

// ---------------------------------------------------------------- servidor
EnetServer::EnetServer(std::uint16_t port, std::size_t maxClients) {
  acquireEnet();
  ENetAddress addr{};
  addr.host = ENET_HOST_ANY;
  addr.port = port;
  host_ = enet_host_create(&addr, maxClients, kChannels, 0, 0);
  if (!host_) {
    releaseEnet();
    throw std::runtime_error("não foi possível escutar na porta " + std::to_string(port));
  }
  ENetAddress bound{};
  port_ = enet_socket_get_address(host_->socket, &bound) == 0 ? bound.port : port;
}

EnetServer::~EnetServer() {
  if (host_) {
    for (auto& [id, peer] : peers_) enet_peer_disconnect_now(peer, 0);
    enet_host_flush(host_);
    enet_host_destroy(host_);
  }
  releaseEnet();
}

void EnetServer::service() {
  ENetEvent ev;
  while (enet_host_service(host_, &ev, 0) > 0) {
    switch (ev.type) {
      case ENET_EVENT_TYPE_CONNECT: {
        const std::uint32_t id = nextId_++;
        ev.peer->data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(id));
        enet_peer_timeout(ev.peer, 0, 5000, 15000);
        peers_[id] = ev.peer;
        inbox_.push_back({Message::Kind::Connected, ConnectionId{id}, Channel::Commands, {}});
        break;
      }
      case ENET_EVENT_TYPE_DISCONNECT: {
        const auto id = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(ev.peer->data));
        peers_.erase(id);
        inbox_.push_back({Message::Kind::Disconnected, ConnectionId{id}, Channel::Commands, {}});
        break;
      }
      case ENET_EVENT_TYPE_RECEIVE: {
        const auto id = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(ev.peer->data));
        inbox_.push_back(dataMessage(ConnectionId{id}, ev));
        enet_packet_destroy(ev.packet);
        break;
      }
      case ENET_EVENT_TYPE_NONE: break;
    }
  }
}

void EnetServer::send(ConnectionId to, Channel channel, std::span<const std::byte> bytes) {
  const auto it = peers_.find(to.value);
  if (it != peers_.end()) sendTo(it->second, channel, bytes);
}

bool EnetServer::poll(Message& out) {
  if (inbox_.empty()) service();
  if (inbox_.empty()) return false;
  out = std::move(inbox_.front());
  inbox_.pop_front();
  return true;
}

void EnetServer::flush() { enet_host_flush(host_); }

// ---------------------------------------------------------------- cliente
EnetClient::EnetClient(const std::string& host, std::uint16_t port) {
  acquireEnet();
  host_ = enet_host_create(nullptr, 1, kChannels, 0, 0);
  if (!host_) {
    releaseEnet();
    throw std::runtime_error("enet_host_create (cliente) falhou");
  }
  ENetAddress addr{};
  if (enet_address_set_host(&addr, host.c_str()) != 0) {
    enet_host_destroy(host_);
    releaseEnet();
    throw std::runtime_error("endereço desconhecido: " + host);
  }
  addr.port = port;
  peer_ = enet_host_connect(host_, &addr, kChannels, 0);
  if (!peer_) {
    enet_host_destroy(host_);
    releaseEnet();
    throw std::runtime_error("enet_host_connect falhou");
  }
  enet_peer_timeout(peer_, 0, 5000, 15000);
}

EnetClient::~EnetClient() {
  if (host_) {
    if (peer_ && connected_) {
      enet_peer_disconnect(peer_, 0);
      // dá ao servidor a chance de ver a saída limpa (até ~200 ms, mesmo com snapshots ainda chegando)
      ENetEvent ev;
      for (int i = 0; i < 40; ++i) {
        const int r = enet_host_service(host_, &ev, 5);
        if (r < 0 || (r > 0 && ev.type == ENET_EVENT_TYPE_DISCONNECT)) break;
        if (r > 0 && ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
      }
    }
    enet_host_destroy(host_);
  }
  releaseEnet();
}

void EnetClient::service() {
  ENetEvent ev;
  while (enet_host_service(host_, &ev, 0) > 0) {
    switch (ev.type) {
      case ENET_EVENT_TYPE_CONNECT:
        connected_ = true;
        inbox_.push_back({Message::Kind::Connected, kServer, Channel::Commands, {}});
        for (const auto& [ch, bytes] : pending_) sendTo(peer_, ch, bytes);
        pending_.clear();
        enet_host_flush(host_);
        break;
      case ENET_EVENT_TYPE_DISCONNECT:
        connected_ = false;
        inbox_.push_back({Message::Kind::Disconnected, kServer, Channel::Commands, {}});
        break;
      case ENET_EVENT_TYPE_RECEIVE:
        inbox_.push_back(dataMessage(kServer, ev));
        enet_packet_destroy(ev.packet);
        break;
      case ENET_EVENT_TYPE_NONE: break;
    }
  }
}

void EnetClient::send(ConnectionId, Channel channel, std::span<const std::byte> bytes) {
  if (!connected_) {
    // comandos perdem a validade; pedidos (Hello) esperam a conexão abrir
    if (channel != Channel::Commands) pending_.emplace_back(channel, std::vector<std::byte>(bytes.begin(), bytes.end()));
    return;
  }
  sendTo(peer_, channel, bytes);
}

bool EnetClient::poll(Message& out) {
  if (inbox_.empty()) service();
  if (inbox_.empty()) return false;
  out = std::move(inbox_.front());
  inbox_.pop_front();
  return true;
}

void EnetClient::flush() { enet_host_flush(host_); }

bool parseAddress(const std::string& text, std::string& host, std::uint16_t& port, std::uint16_t defaultPort) {
  const std::size_t colon = text.rfind(':');
  host = colon == std::string::npos ? text : text.substr(0, colon);
  port = defaultPort;
  if (colon != std::string::npos) {
    const char* b = text.data() + colon + 1;
    const char* e = text.data() + text.size();
    unsigned v = 0;
    const auto [p, ec] = std::from_chars(b, e, v);
    if (ec != std::errc{} || p != e || v == 0 || v > 65535) return false;
    port = static_cast<std::uint16_t>(v);
  }
  return !host.empty();
}

}  // namespace rpg::net
