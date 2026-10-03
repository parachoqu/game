#pragma once
// Ações discretas do jogador (canal Requests, confiável e ordenado). Na demo eram chamadas diretas das
// telas para a simulação (`panels.js` importava `sell`, `buy`, `craft`…); aqui viram dados, e o
// servidor confere tudo (distância do serviço, moedas, capacidade, combate) antes de aplicar.
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "core/protocol/ByteStream.h"

namespace rpg::protocol {

// Clique num inimigo: aproximar e atacar até ele cair.
struct ReqClickAttack {
  std::uint32_t enemy = 0;
  template <class A> void io(A& a) { a(enemy); }
};
// Clique num alvo de uso (serviço, coleta, carga, viajante, portal, montaria, santuário, saída).
struct ReqClickUse {
  std::uint8_t kind = 0;  // sim::InteractKind
  std::uint32_t id = 0;
  template <class A> void io(A& a) { a(kind); a(id); }
};
struct ReqUsePotion { template <class A> void io(A&) {} };   // barra de ações
struct ReqMountAction { template <class A> void io(A&) {} }; // barra de ações

// ---------------------------------------------------------------- painéis de serviço
struct ReqBuy {
  std::uint16_t market = 0, item = 0;
  std::int32_t qty = 1;
  template <class A> void io(A& a) { a(market); a(item); a(qty); }
};
struct ReqSell {
  std::uint16_t market = 0, item = 0;
  std::int32_t qty = 1;
  bool all = false;  // "Todos": vende o que houver na bolsa
  template <class A> void io(A& a) { a(market); a(item); a(qty); a(all); }
};
struct ReqCraft {
  std::uint16_t recipe = 0;
  bool useStorage = true;
  template <class A> void io(A& a) { a(recipe); a(useStorage); }
};
struct ReqRepair {
  std::uint8_t slot = 0;  // 0 arma, 1 armadura
  template <class A> void io(A& a) { a(slot); }
};
struct ReqLearn {
  std::uint16_t training = 0;
  template <class A> void io(A& a) { a(training); }
};
struct ReqRestartKit { template <class A> void io(A&) {} };
struct ReqStore {
  std::uint32_t index = 0;
  bool all = false;
  template <class A> void io(A& a) { a(index); a(all); }
};
struct ReqStoreMaterials { template <class A> void io(A&) {} };
struct ReqWithdraw {
  std::uint32_t index = 0;
  bool all = false;
  template <class A> void io(A& a) { a(index); a(all); }
};
struct ReqBuyMount { template <class A> void io(A&) {} };
struct ReqStableBagsToStorage { template <class A> void io(A&) {} };
struct ReqDeliver {
  std::uint16_t item = 0;
  template <class A> void io(A& a) { a(item); }
};
struct ReqGiveInstrument { template <class A> void io(A&) {} };
struct ReqTakeLoot {
  std::uint32_t bag = 0;
  std::int32_t index = -1;  // -1 = pegar tudo
  template <class A> void io(A& a) { a(bag); a(index); }
};
struct ReqEnterPortal { template <class A> void io(A&) {} };
struct ReqRespawn { template <class A> void io(A&) {} };

// ---------------------------------------------------------------- inventário
struct ReqEquip {
  std::uint32_t index = 0;
  template <class A> void io(A& a) { a(index); }
};
struct ReqUnequip {
  std::uint8_t slot = 0;  // 0 arma, 1 armadura, 2 bolsa
  template <class A> void io(A& a) { a(slot); }
};
struct ReqDrink {
  std::uint32_t index = 0;
  template <class A> void io(A& a) { a(index); }
};
struct ReqToSaddle {
  std::uint32_t index = 0;
  template <class A> void io(A& a) { a(index); }
};
struct ReqFromSaddle {
  std::uint32_t index = 0;
  template <class A> void io(A& a) { a(index); }
};
struct ReqDrop {
  std::uint32_t index = 0;
  template <class A> void io(A& a) { a(index); }
};

// ---------------------------------------------------------------- céu, Livro e sessão
struct ReqEndObserve { template <class A> void io(A&) {} };
struct ReqBookSetPublic {
  std::int32_t entry = 0;
  bool isPublic = false;
  template <class A> void io(A& a) { a(entry); a(isPublic); }
};
struct ReqBookNote {
  std::string text;
  template <class A> void io(A& a) { a(text); }
};
struct ReqShowTitle {
  std::optional<std::string> title;
  template <class A> void io(A& a) { a(title); }
};
// Pausa: só vale no jogo local de um jogador (ServerConfig::allowPause).
struct ReqPause {
  bool paused = false;
  template <class A> void io(A& a) { a(paused); }
};

// Sair do mundo sem desconectar (pausa → "Nova trajetória"): o servidor salva o personagem e o tira do
// mundo; a sessão fica livre para um novo Hello. `discard` apaga o save em vez de gravar (save.js clearSave).
struct ReqLeave {
  bool discard = false;
  template <class A> void io(A& a) { a(discard); }
};

// Comandos de desenvolvimento (window.__demo): só aceitos com o servidor em modo de desenvolvimento.
enum class DevCommand : std::uint8_t { Teleport, Give, Coins, Hurt, Portal, KillNear, Contribute, Night, Vanish };
struct ReqDev {
  DevCommand cmd = DevCommand::Coins;
  double a = 0, b = 0;
  std::uint16_t item = 0;
  template <class A> void io(A& a_) { a_(cmd); a_(a); a_(b); a_(item); }
};

using Request = std::variant<ReqClickAttack, ReqClickUse, ReqUsePotion, ReqMountAction, ReqBuy, ReqSell, ReqCraft,
                             ReqRepair, ReqLearn, ReqRestartKit, ReqStore, ReqStoreMaterials, ReqWithdraw, ReqBuyMount,
                             ReqStableBagsToStorage, ReqDeliver, ReqGiveInstrument, ReqTakeLoot, ReqEnterPortal,
                             ReqRespawn, ReqEquip, ReqUnequip, ReqDrink, ReqToSaddle, ReqFromSaddle, ReqDrop,
                             ReqEndObserve, ReqBookSetPublic, ReqBookNote, ReqShowTitle, ReqPause, ReqDev, ReqLeave>;

}  // namespace rpg::protocol
