#pragma once
// Mensagens de sessão: entrada no jogo e o envelope de cada canal.
//
//   Requests  (cliente → servidor): ClientMessage — Hello uma vez, depois Requests
//   Commands  (cliente → servidor): PlayerCommand
//   Events    (servidor → cliente): ServerMessage — Welcome/Reject, depois GameEvents
//   Snapshots (servidor → cliente): Snapshot
#include <cstdint>
#include <string>
#include <variant>

#include "core/protocol/ByteStream.h"
#include "core/protocol/GameEvents.h"
#include "core/protocol/Requests.h"
#include "core/protocol/Snapshot.h"

namespace rpg::protocol {

struct Hello {
  std::uint16_t protocol = kProtocolVersion;
  std::string dataHash;  // resumo dos dados de design do cliente; tem que bater com o do servidor
  std::string name, origin, start, model;
  bool resume = false;   // retomar o personagem salvo com este nome (quando houver)
  template <class A> void io(A& a) { a(protocol); a(dataHash); a(name); a(origin); a(start); a(model); a(resume); }
};

struct Welcome {
  std::uint32_t playerId = 0;
  double tickRate = 30;
  std::uint64_t tick = 0;
  bool resumed = false;
  template <class A> void io(A& a) { a(playerId); a(tickRate); a(tick); a(resumed); }
};

struct Reject {
  std::string reason;
  template <class A> void io(A& a) { a(reason); }
};

using ClientMessage = std::variant<Hello, Request>;
using ServerMessage = std::variant<Welcome, Reject, GameEvent>;

}  // namespace rpg::protocol
