#pragma once
// Cliente de teste: fala com o ServerHost pelo LocalTransport, só com bytes do protocolo (como o jogo).
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/protocol/Session.h"
#include "net/Bytes.h"
#include "net/LocalTransport.h"

namespace rpg::test {

struct TestClient {
  std::unique_ptr<net::ITransport> t;
  protocol::PlayerCommand cmd;
  std::vector<protocol::ServerMessage> events;
  std::optional<protocol::Snapshot> snapshot;
  std::optional<protocol::Welcome> welcome;
  std::optional<std::string> rejected;

  explicit TestClient(std::unique_ptr<net::ITransport> transport) : t(std::move(transport)) {}

  void sendBytes(net::Channel ch, const protocol::Bytes& b) { t->send(net::kServer, ch, net::asBytes(b)); }

  void hello(const std::string& name, const std::string& start = "espadachim", bool resume = false) {
    protocol::Hello h;
    h.name = name;
    h.origin = "humano";
    h.start = start;
    h.model = "kachujin";
    h.resume = resume;
    welcome.reset();
    rejected.reset();
    sendBytes(net::Channel::Requests, protocol::encode(protocol::ClientMessage{h}));
  }
  void request(const protocol::Request& r) { sendBytes(net::Channel::Requests, protocol::encode(protocol::ClientMessage{r})); }
  void dev(protocol::DevCommand c, double a = 0, double b = 0, std::uint16_t item = 0) {
    request(protocol::ReqDev{c, a, b, item});
  }
  void command() {
    cmd.seq++;
    sendBytes(net::Channel::Commands, protocol::encode(cmd));
  }
  void press(protocol::Press p) {
    cmd.press(p);
    command();
  }

  void drain() {
    net::Message m;
    while (t->poll(m)) {
      if (m.kind != net::Message::Kind::Data) continue;
      if (m.channel == net::Channel::Events) {
        auto e = protocol::decode<protocol::ServerMessage>(net::asU8(m.bytes));
        REQUIRE(e);
        if (auto* w = std::get_if<protocol::Welcome>(&*e)) welcome = *w;
        if (auto* r = std::get_if<protocol::Reject>(&*e)) rejected = r->reason;
        events.push_back(std::move(*e));
      } else if (m.channel == net::Channel::Snapshots) {
        auto s = protocol::decode<protocol::Snapshot>(net::asU8(m.bytes));
        REQUIRE(s);
        snapshot = std::move(*s);
      }
    }
  }

  // Eventos de jogo de um tipo, desde o último clear().
  template <class T>
  std::vector<T> gameEvents() const {
    std::vector<T> out;
    for (const auto& e : events)
      if (const auto* g = std::get_if<protocol::GameEvent>(&e))
        if (const auto* v = std::get_if<T>(g)) out.push_back(*v);
    return out;
  }
};

// Diretório temporário apagado no fim do teste.
struct TempDir {
  std::filesystem::path path;
  explicit TempDir(const std::string& tag) {
    std::random_device rd;
    path = std::filesystem::temp_directory_path() / ("rpg-" + tag + "-" + std::to_string(rd()));
    std::filesystem::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

}  // namespace rpg::test
