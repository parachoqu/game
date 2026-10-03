#pragma once
// Transporte entre cliente e servidor. Tudo o que cruza a fronteira vai em bytes, mesmo quando os dois
// lados estão no mesmo processo: assim nenhuma camada acima depende de compartilhar memória, e trocar
// o LocalTransport pela rede de verdade (fase online) não muda o jogo.
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rpg::net {

// Canais com garantias diferentes (espelham as mensagens do protocolo).
enum class Channel : std::uint8_t {
  Commands,   // PlayerCommand a cada tick: não confiável (o próximo substitui o perdido)
  Requests,   // ações discretas (comprar, fabricar…): confiável e em ordem
  Events,     // eventos do jogo para a apresentação: confiável e em ordem
  Snapshots,  // estado replicado: não confiável
  Count,
};

constexpr bool isReliable(Channel c) { return c == Channel::Requests || c == Channel::Events; }

struct ConnectionId {
  std::uint32_t value = 0;
  constexpr auto operator<=>(const ConnectionId&) const = default;
};

// Do ponto de vista do cliente, o servidor é sempre esta conexão.
inline constexpr ConnectionId kServer{0};

struct Message {
  enum class Kind : std::uint8_t { Connected, Disconnected, Data };
  Kind kind = Kind::Data;
  ConnectionId peer;          // de quem veio (no servidor: o cliente; no cliente: kServer)
  Channel channel = Channel::Commands;
  std::vector<std::byte> bytes;
};

class ITransport {
 public:
  virtual ~ITransport() = default;

  // Envia para `to` (o cliente ignora `to` e manda sempre ao servidor).
  virtual void send(ConnectionId to, Channel channel, std::span<const std::byte> bytes) = 0;

  // Próxima mensagem já entregue, se houver. Não bloqueia.
  virtual bool poll(Message& out) = 0;
};

}  // namespace rpg::net
