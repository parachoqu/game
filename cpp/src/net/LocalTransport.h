#pragma once
// Transporte em processo: o servidor e um ou mais clientes trocam bytes por filas protegidas por mutex.
// É o que o jogo local usa agora (servidor numa thread, cliente na principal).
//
// `NetSim` injeta latência, variação e perda para exercitar a rede sem sair do processo:
//   - canais confiáveis (Requests, Events) nunca perdem mensagem e mantêm a ordem por remetente;
//   - canais não confiáveis (Commands, Snapshots) podem perder e chegar fora de ordem, como no UDP.
#include <functional>
#include <memory>

#include "net/Transport.h"

namespace rpg::net {

struct NetSim {
  double latencyMs = 0.0;    // atraso fixo de ida
  double jitterMs = 0.0;     // atraso extra sorteado em [0, jitter)
  double lossPercent = 0.0;  // perda nos canais não confiáveis
  std::uint64_t seed = 1;    // sorteios determinísticos (testes reproduzíveis)
};

// Relógio em segundos. O padrão é steady_clock; os testes injetam um relógio manual.
using TimeSource = std::function<double()>;
double steadySeconds();

class LocalHub {
 public:
  explicit LocalHub(NetSim sim = {}, TimeSource now = steadySeconds);
  ~LocalHub();
  LocalHub(const LocalHub&) = delete;
  LocalHub& operator=(const LocalHub&) = delete;

  // Ponta do servidor. Só pode ser pedida uma vez.
  std::unique_ptr<ITransport> serverEndpoint();

  // Conecta um cliente novo: o servidor recebe `Connected` com o ConnectionId dele. Destruir a ponta
  // do cliente desconecta (o servidor recebe `Disconnected`).
  std::unique_ptr<ITransport> connectClient();

  struct State;

 private:
  std::shared_ptr<State> state_;
  bool serverTaken_ = false;
};

}  // namespace rpg::net
