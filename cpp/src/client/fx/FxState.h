#pragma once
// Apresentação dos eventos do jogo (engine/fx.js + os `on(...)` de main.js e hud.js): textos
// flutuantes, telegrafias no chão, partículas, avisos, marcador de acerto, painéis que o servidor
// abre, relatório de derrota. A aleatoriedade daqui (partículas) é só visual.
#include <cstdint>
#include <deque>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/protocol/GameEvents.h"

namespace rpg::client {

class Localization;

struct FloatFx {
  glm::dvec3 pos{0.0};
  std::string text;
  protocol::FloatKind kind = protocol::FloatKind::Dmg;
  double t = 0;
};

struct TelegraphFx {
  protocol::EvTelegraph ev;
  double t = 0;
  bool cancelled = false;
};

struct ParticleFx {
  glm::dvec3 pos{0.0}, vel{0.0};
  std::uint32_t color = 0xffd27a;
  double life = 0.5;
};

struct ToastFx {
  std::string text;
  protocol::ToastKind kind = protocol::ToastKind::Info;
  double left = 4.2, total = 4.2;
};

class FxState {
 public:
  static constexpr std::size_t kMaxToasts = 4;

  explicit FxState(const Localization& L) : L_(&L) {}

  void apply(const protocol::GameEvent& ev);
  void update(double dt);
  void toast(std::string text, protocol::ToastKind kind = protocol::ToastKind::Info, double seconds = 4.2);
  void clearWorld();  // troca de mapa / reaparecer: telegrafias e partículas somem

  std::vector<FloatFx> floats;
  std::vector<TelegraphFx> telegraphs;
  std::vector<ParticleFx> particles;
  std::deque<ToastFx> toasts;  // o mais novo primeiro

  double hitmarker = 0;  // s restantes do marcador de acerto
  bool hitmarkerCrit = false;
  double hurtFlash = 0;  // vinheta vermelha ao levar dano

  // Pedidos à câmera e ao áudio, consumidos pelo app a cada quadro.
  double camRecoilPitch = 0, camRecoilYaw = 0, camShake = 0;
  std::vector<protocol::SoundId> sounds;

  // Faixa de zona: expande por 7 s na troca (hud.js `showZone`).
  std::optional<std::uint8_t> zoneChangedTo;
  double zoneExpand = 0;

  // Telas que o servidor pede
  std::optional<protocol::DeathReport> death;
  double deathDelay = 0;  // main.js: o painel aparece 0,7 s depois
  std::optional<protocol::EvServiceOpened> service;
  std::optional<protocol::EvEventDone> eventDone;
  std::optional<protocol::EvObserve> observe;
  bool respawned = false;      // o app volta a câmera para o norte
  bool turbEntered = false, turbLeft = false;

  // Livro (o cliente guarda uma cópia para a tela do Livro)
  std::vector<protocol::BookEntry> book;
  std::vector<protocol::BookTitle> titles;
  std::optional<std::string> shownTitle;

 private:
  const Localization* L_;
  std::mt19937 rng_{12345};
};

}  // namespace rpg::client
