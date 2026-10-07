#pragma once
// Transporte pela rede (ENet sobre UDP), com os quatro canais do protocolo:
//
//   Commands   não confiável, em sequência (o comando atrasado é descartado pelo mais novo)
//   Requests   confiável e em ordem
//   Events     confiável e em ordem
//   Snapshots  não confiável, em sequência, fragmentos também não confiáveis (um fragmento perdido
//              perde o snapshot inteiro, e o próximo o substitui)
//
// O servidor escuta numa porta; o cliente conecta a um endereço. Os dois só falam bytes, como o
// LocalTransport: o ServerHost e a ClientSession não sabem qual dos dois estão usando.
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "net/Transport.h"

struct _ENetHost;
struct _ENetPeer;

namespace rpg::net {

// Porta padrão do servidor dedicado (rpg_server --listen, rpg_local --connect).
inline constexpr std::uint16_t kDefaultPort = 27450;

class EnetServer final : public ITransport {
 public:
  // Porta 0: o sistema escolhe (veja port()). Lança std::runtime_error se não der para escutar.
  explicit EnetServer(std::uint16_t port, std::size_t maxClients = 64);
  ~EnetServer() override;

  std::uint16_t port() const { return port_; }
  std::size_t clients() const { return peers_.size(); }

  void send(ConnectionId to, Channel channel, std::span<const std::byte> bytes) override;
  bool poll(Message& out) override;
  void flush() override;

 private:
  void service();

  _ENetHost* host_ = nullptr;
  std::uint16_t port_ = 0;
  std::uint32_t nextId_ = 1;
  std::unordered_map<std::uint32_t, _ENetPeer*> peers_;
  std::deque<Message> inbox_;
};

class EnetClient final : public ITransport {
 public:
  // Conecta em segundo plano; Connected (ou Disconnected, se não der) chega pelo poll().
  EnetClient(const std::string& host, std::uint16_t port);
  ~EnetClient() override;

  bool connected() const { return connected_; }

  void send(ConnectionId to, Channel channel, std::span<const std::byte> bytes) override;
  bool poll(Message& out) override;
  void flush() override;

 private:
  void service();

  _ENetHost* host_ = nullptr;
  _ENetPeer* peer_ = nullptr;
  bool connected_ = false;
  std::deque<Message> inbox_;
  // o que foi enviado antes de a conexão terminar de abrir
  std::vector<std::pair<Channel, std::vector<std::byte>>> pending_;
};

// "host:porta" → partes (porta padrão quando não vem).
bool parseAddress(const std::string& text, std::string& host, std::uint16_t& port, std::uint16_t defaultPort);

}  // namespace rpg::net
