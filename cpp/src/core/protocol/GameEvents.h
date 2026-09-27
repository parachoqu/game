#pragma once
// O que a simulação conta ao cliente além do estado (canal Events, confiável e ordenado).
//
// Na demo a lógica chamava a apresentação direto: `sfx('hit')`, `burst(…)`, `floatText(…)`,
// `telegraph(…)`, `toast(…)`, `emit('show-death')`. Aqui cada uma dessas chamadas vira um evento
// semântico; o cliente decide som, partícula, texto e tela. O servidor filtra por destinatário
// (o dono, quem está perto, todos no mapa).
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "core/protocol/BookData.h"
#include "core/protocol/ByteStream.h"
#include "core/protocol/Messages.h"

namespace rpg::protocol {

struct EvSound {
  SoundId sound = SoundId::Ui;
  template <class A> void io(A& a) { a(sound); }
};

struct EvToast {
  MessageId msg = MessageId::CoinsInsufficient;
  MsgArgs args;
  ToastKind kind = ToastKind::Info;
  std::uint16_t durationMs = 0;  // 0 = padrão do HUD
  template <class A> void io(A& a) { a(msg); a(args); a(kind); a(durationMs); }
};

struct EvFloatText {
  double x = 0, y = 0, z = 0;
  FloatKind kind = FloatKind::Dmg;
  MessageId msg = MessageId::FloatNumber;
  MsgArgs args;
  template <class A> void io(A& a) { a.f32(x); a.f32(y); a.f32(z); a(kind); a(msg); a(args); }
};

struct EvBurst {
  double x = 0, y = 0, z = 0;
  std::uint32_t color = 0xffd27a;  // RGB
  std::uint8_t count = 10;
  double speed = 5;
  template <class A> void io(A& a) { a.f32(x); a.f32(y); a.f32(z); a(color); a(count); a.f32(speed); }
};

enum class TeleShape : std::uint8_t { Circle, Sector, Line };
struct EvTelegraph {
  std::uint32_t id = 0;
  TeleShape shape = TeleShape::Circle;
  std::uint8_t map = 0;
  double x = 0, z = 0, radius = 0, arc = 0, length = 0, width = 0, facing = 0, duration = 0;
  std::uint32_t color = 0xff5a3c;
  template <class A>
  void io(A& a) {
    a(id); a(shape); a(map);
    a.f32(x); a.f32(z); a.f32(radius); a.f32(arc); a.f32(length); a.f32(width); a.f32(facing); a.f32(duration);
    a(color);
  }
};
struct EvTelegraphCancel {
  std::uint32_t id = 0;
  template <class A> void io(A& a) { a(id); }
};

struct EvHitmarker {
  bool crit = false;
  template <class A> void io(A& a) { a(crit); }
};
// Recuo da câmera no próprio disparo: o desvio lateral é sorteado pelo cliente (aleatoriedade visual).
struct EvCamRecoil {
  double pitch = 0.03, yawJitter = 0;
  template <class A> void io(A& a) { a.f32(pitch); a.f32(yawJitter); }
};
struct EvCamShake {
  double amount = 0.3;
  template <class A> void io(A& a) { a.f32(amount); }
};

struct EvZoneChanged {
  std::uint8_t from = 0, to = 0;  // ZoneKind
  template <class A> void io(A& a) { a(from); a(to); }
};
struct EvPlaceChanged {
  std::uint8_t place = 0;  // PlaceId
  template <class A> void io(A& a) { a(place); }
};
struct EvPlayerHurt {
  double amount = 0;
  template <class A> void io(A& a) { a.f32(amount); }
};
struct EvPlayerDown { template <class A> void io(A&) {} };

// Relatório de derrota (zones.js `handleDeath`).
struct DeathLine {
  std::uint16_t item = 0;
  std::int32_t qty = 1;
  template <class A> void io(A& a) { a(item); a(qty); }
};
struct DeathReport {
  std::uint8_t zone = 0;   // ZoneKind
  std::uint8_t place = 0;  // PlaceId
  MsgArg cause;            // inimigo (Enemy) ou causa fixa (Message)
  std::vector<DeathLine> lost, destroyed;
  std::vector<MsgArg> kept;  // o que permanece (mensagens com argumentos simples)
  std::int32_t coinsKept = 0;
  bool bag = false;
  bool hasWeapon = true;
  template <class A> void io(A& a) { a(zone); a(place); a(cause); a(lost); a(destroyed); a(kept); a(coinsKept); a(bag); a(hasWeapon); }
};
struct EvPlayerDied {
  DeathReport report;
  template <class A> void io(A& a) { a(report); }
};
struct EvRespawned { template <class A> void io(A&) {} };

// Abertura de serviço: o cliente mostra o painel correspondente.
enum class Service : std::uint8_t {
  Market, Forge, Trainer, Storage, Stable, Board, Canteiro, Astronomer, Loot, Traveler, Portal,
};
struct EvServiceOpened {
  Service service = Service::Market;
  std::uint32_t data = 0;  // mercado (MarketId), saco (EntityId), viajante (índice)
  template <class A> void io(A& a) { a(service); a(data); }
};

// As quatro perguntas do evento regional (event.js `fourQuestions`).
struct ContributionPart {
  std::string kind;  // ferramentas, lingotes, reconhecimento, saqueadores, acampamento
  double pts = 0;
  template <class A> void io(A& a) { a(kind); a(pts); }
};
struct EvEventDone {
  std::int32_t you = 0, others = 0;  // %
  std::vector<ContributionPart> parts;
  template <class A> void io(A& a) { a(you); a(others); a(parts); }
};

// Observação do céu (stars.js `observe`): legenda e estrela enquadrada.
enum class ObserveCaption : std::uint8_t { Intact, Daylight, Absence };
struct EvObserve {
  ObserveCaption caption = ObserveCaption::Intact;
  std::int32_t star = 0;
  std::int32_t constellation = -1;
  bool gap = false;
  template <class A> void io(A& a) { a(caption); a(star); a(constellation); a(gap); }
};
struct EvObserveEnd { template <class A> void io(A&) {} };

struct EvStarVanished {
  std::int32_t star = 0;
  template <class A> void io(A& a) { a(star); }
};
struct EvTurbEnter { template <class A> void io(A&) {} };
struct EvTurbLeave {
  bool success = false;
  template <class A> void io(A& a) { a(success); }
};

// Livro: entrada nova ou atualizada; título novo; o Livro inteiro (entrada no jogo, save carregado).
struct EvBookEntry {
  BookEntry entry;
  bool created = true;
  template <class A> void io(A& a) { a(entry); a(created); }
};
struct EvBookTitle {
  std::string title;
  template <class A> void io(A& a) { a(title); }
};
struct EvBookSync {
  std::vector<BookEntry> entries;
  std::vector<BookTitle> titles;
  std::optional<std::string> shownTitle;
  template <class A> void io(A& a) { a(entries); a(titles); a(shownTitle); }
};

using GameEvent = std::variant<EvSound, EvToast, EvFloatText, EvBurst, EvTelegraph, EvTelegraphCancel, EvHitmarker,
                               EvCamRecoil, EvCamShake, EvZoneChanged, EvPlaceChanged, EvPlayerHurt, EvPlayerDown,
                               EvPlayerDied, EvRespawned, EvServiceOpened, EvEventDone, EvObserve, EvObserveEnd,
                               EvStarVanished, EvTurbEnter, EvTurbLeave, EvBookEntry, EvBookTitle, EvBookSync>;

}  // namespace rpg::protocol
