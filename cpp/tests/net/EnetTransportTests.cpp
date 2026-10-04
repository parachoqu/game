#include <chrono>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "net/Bytes.h"
#include "net/EnetTransport.h"

using namespace rpg::net;

namespace {

// Roda os dois lados até `done` ou 3 s.
template <class F>
bool pumpUntil(EnetServer& s, EnetClient& c, std::vector<Message>& serverIn, std::vector<Message>& clientIn, F done) {
  for (int i = 0; i < 300 && !done(); ++i) {
    Message m;
    while (s.poll(m)) serverIn.push_back(m);
    while (c.poll(m)) clientIn.push_back(m);
    s.flush();
    c.flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return done();
}

}  // namespace

TEST_CASE("ENet: conecta, troca bytes nos quatro canais e percebe a saída", "[net][enet]") {
  EnetServer server(0);
  REQUIRE(server.port() != 0);
  auto client = std::make_unique<EnetClient>("127.0.0.1", server.port());
  std::vector<Message> sIn, cIn;
  const std::vector<std::byte> hello{std::byte{1}, std::byte{2}, std::byte{3}};
  client->send(kServer, Channel::Requests, hello);  // antes de conectar: espera a conexão abrir
  REQUIRE(pumpUntil(server, *client, sIn, cIn, [&] { return client->connected() && sIn.size() >= 2; }));
  CHECK(sIn[0].kind == Message::Kind::Connected);
  const ConnectionId id = sIn[0].peer;
  CHECK(sIn[1].kind == Message::Kind::Data);
  CHECK(sIn[1].channel == Channel::Requests);
  CHECK(sIn[1].bytes == hello);
  CHECK(server.clients() == 1);

  // snapshot grande (fragmentado) do servidor ao cliente; evento confiável
  std::vector<std::byte> big(6000);
  for (std::size_t i = 0; i < big.size(); ++i) big[i] = static_cast<std::byte>(i * 7);
  server.send(id, Channel::Snapshots, big);
  server.send(id, Channel::Events, hello);
  REQUIRE(pumpUntil(server, *client, sIn, cIn, [&] {
    int data = 0;
    for (const Message& m : cIn) data += m.kind == Message::Kind::Data;
    return data >= 2;
  }));
  bool gotBig = false, gotEvent = false;
  for (const Message& m : cIn) {
    if (m.kind != Message::Kind::Data) continue;
    gotBig |= m.channel == Channel::Snapshots && m.bytes == big;
    gotEvent |= m.channel == Channel::Events && m.bytes == hello;
  }
  CHECK(gotBig);
  CHECK(gotEvent);

  // o cliente sai: o servidor vê Disconnected
  client.reset();
  std::vector<Message> rest;
  for (int i = 0; i < 200; ++i) {
    Message m;
    bool done = false;
    while (server.poll(m)) {
      rest.push_back(m);
      done |= m.kind == Message::Kind::Disconnected;
    }
    if (done) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  REQUIRE_FALSE(rest.empty());
  CHECK(rest.back().kind == Message::Kind::Disconnected);
  CHECK(server.clients() == 0);
}

TEST_CASE("ENet: endereço host:porta", "[net][enet]") {
  std::string host;
  std::uint16_t port = 0;
  CHECK(parseAddress("jogo.exemplo:4777", host, port, 1));
  CHECK(host == "jogo.exemplo");
  CHECK(port == 4777);
  CHECK(parseAddress("127.0.0.1", host, port, 4777));
  CHECK(port == 4777);
  CHECK_FALSE(parseAddress("x:0", host, port, 1));
  CHECK_FALSE(parseAddress("x:99999", host, port, 1));
  CHECK_FALSE(parseAddress(":4777", host, port, 1));
}
