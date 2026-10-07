#pragma once
// O que o HUD desenha em modo imediato, por cima do mundo: as partes de fx.js presas a posições 3D
// (textos flutuantes, barras sobre inimigos). O resto do HUD (hud.js) e os painéis são o documento
// RmlUi (ver GameUi).
#include <cstdint>
#include <optional>
#include <vector>

#include "client/ClientWorld.h"
#include "client/Input.h"
#include "client/game/Targets.h"
#include "client/game/ViewCamera.h"
#include "client/ui/UiBatch.h"
#include "core/GameClock.h"

namespace rpg::client {

class FxState;
class Localization;
struct Presentation;

struct HudContext {
  const ClientWorld* world = nullptr;
  const FxState* fx = nullptr;
  const Localization* L = nullptr;
  const Presentation* look = nullptr;
  const GameData* data = nullptr;
  const StaticWorld* statics = nullptr;
  const ViewCamera* camera = nullptr;
  const InputState* input = nullptr;
  ClockConfig clock;
  std::optional<UseTarget> prompt;
  std::optional<Hover> hover;
  bool aiming = false;
  double aimDist = 0;
  std::uint32_t aimEnemy = 0;
  bool showKeys = true;
};

// Estilo comum de painel (moldura escura com filete de bronze).
void drawPanel(UiBatch& ui, float x, float y, float w, float h, Rgba accent = Rgba::hex(0x3a4a55));
// Botão imediato: desenha e devolve true no clique.
bool drawButton(UiBatch& ui, const UiFonts& f, const InputState& in, float x, float y, float w, float h, std::string_view label,
                bool primary = false, bool enabled = true);

class Hud {
 public:
  // Desenha o HUD. Devolve um pedido gerado por clique (barra de ações, relatório de derrota).
  enum class Action : std::uint8_t { None, Potion, Mount, Respawn, CamNorth };
  Action draw(UiBatch& ui, const UiFonts& fonts, const HudContext& ctx);

  std::string useLabel(const UseTarget& t, const HudContext& ctx) const;

  // O ponto (pixels) está sobre um painel do HUD desenhado no último quadro (o clique é da interface).
  bool blocks(double x, double y) const;

 private:
  struct Rect {
    float x, y, w, h;
  };
  std::vector<Rect> blockers_;
};

}  // namespace rpg::client
