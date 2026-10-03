#include "client/ui/Hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "client/Presentation.h"
#include "client/fx/FxState.h"
#include "client/ui/Localization.h"
#include "core/data/GameData.h"
#include "core/protocol/Replicated.h"
#include "core/world/StaticWorld.h"

namespace rpg::client {

namespace proto = protocol;
using protocol::MsgArg;

namespace {

const Rgba kBone = Rgba::hex(0xece4d2), kMuted = Rgba::hex(0xa8a193), kBronze = Rgba::hex(0xa5844f);
const Rgba kGood = Rgba::hex(0x7fc98a);
const Rgba kIron = Rgba::hex(0x090f15), kGold = Rgba::hex(0xf2c86a);

std::string fmt(const Localization& L, std::string_view key, const proto::MsgArgs& args) { return L.format(L.ui(key), args); }

}  // namespace

void drawPanel(UiBatch& ui, float x, float y, float w, float h, Rgba accent) {
  ui.rect(x, y, w, h, Rgba{14, 21, 28, 235});
  ui.rect(x, y, w, h * 0.4f, Rgba{24, 34, 44, 90});
  ui.frame(x, y, w, h, 1, kIron);
  ui.frame(x + 1, y + 1, w - 2, h - 2, 1, Rgba{165, 132, 79, 60});
  (void)accent;
}

bool drawButton(UiBatch& ui, const UiFonts& f, const InputState& in, float x, float y, float w, float h, std::string_view label,
                bool primary, bool enabled) {
  const auto mx = static_cast<float>(in.mouseX), my = static_cast<float>(in.mouseY);
  const bool hover = enabled && mx >= x && mx <= x + w && my >= y && my <= y + h;
  Rgba bg = primary ? Rgba{120, 92, 52, 245} : Rgba{27, 39, 51, 240};
  if (hover) bg = primary ? Rgba{150, 116, 64, 250} : Rgba{36, 51, 63, 245};
  if (!enabled) bg = Rgba{20, 26, 32, 200};
  ui.rect(x, y, w, h, bg);
  ui.frame(x, y, w, h, 1, primary ? kBronze : Rgba{58, 74, 85, 255});
  ui.text(f.bold, 14, x + w / 2, y + (h - 17) / 2, label, enabled ? kBone : kMuted, Align::Center);
  return hover && in.leftPressed;
}

std::string Hud::useLabel(const UseTarget& t, const HudContext& ctx) const {
  const Localization& L = *ctx.L;
  const GameplayLayout& lay = ctx.statics->layout();
  switch (t.kind) {
    case proto::InteractKind::Node: {
      const LayoutInteractable& it = lay.interactables[static_cast<std::size_t>(t.layoutIndex)];
      const LayoutNode& n = lay.nodes[static_cast<std::size_t>(it.node)];
      const std::string verb = L.ui("target.gatherVerb." + n.kind);
      const ItemId id = ctx.data->items.find(n.kind);
      if (!t.usable) {
        const auto* W = ctx.world->world();
        const double left = W && static_cast<std::size_t>(it.node) < W->nodes.size() ? W->nodes[static_cast<std::size_t>(it.node)].regrowAt - W->time : 0;
        return L.message(proto::MessageId::NodeDepleted, {MsgArg::str(verb), MsgArg::integer(static_cast<long long>(std::ceil(std::max(0.0, left))))});
      }
      return L.message(proto::MessageId::NodeAvailable, {MsgArg::str(verb), MsgArg::str(L.item(id))});
    }
    case proto::InteractKind::LootBag: {
      const auto* e = ctx.world->find(t.id);
      return L.ui(e && (e->flags & proto::kFlagOwn) ? "target.lootOwn" : "target.loot");
    }
    case proto::InteractKind::Traveler: {
      const std::string key = t.travelerIndex >= 0 && static_cast<std::size_t>(t.travelerIndex) < lay.travelers.size()
                                  ? lay.travelers[static_cast<std::size_t>(t.travelerIndex)].key
                                  : std::string{};
      return fmt(L, "target.traveler", {MsgArg::str(L.travelerName(key))});
    }
    case proto::InteractKind::Portal: return L.ui("target.portal");
    case proto::InteractKind::Mount: return L.ui("target.mount");
    case proto::InteractKind::Shrine: return L.ui("target.shrine");
    case proto::InteractKind::Exit: return L.ui("target.exit");
    default: {
      if (t.layoutIndex < 0) return {};
      return L.interactable(lay.interactables[static_cast<std::size_t>(t.layoutIndex)].label);
    }
  }
}

bool Hud::blocks(double x, double y) const {
  for (const Rect& r : blockers_)
    if (x >= static_cast<double>(r.x) && x <= static_cast<double>(r.x + r.w) && y >= static_cast<double>(r.y) &&
        y <= static_cast<double>(r.y + r.h))
      return true;
  return false;
}

Hud::Action Hud::draw(UiBatch& ui, const UiFonts& f, const HudContext& ctx) {
  Action action = Action::None;
  blockers_.clear();
  const ClientWorld& w = *ctx.world;
  const FxState& fx = *ctx.fx;
  const Localization& L = *ctx.L;
  const InputState& in = *ctx.input;
  const proto::PrivateState* P = w.self();
  const proto::WorldState* W = w.world();
  const proto::EntityState* me = w.me();
  if (!P || !W || !me) return action;
  const ViewCamera& cam = *ctx.camera;

  // barras sobre inimigos (fx.js `showBar`)
  for (const auto& e : w.entities()) {
    if (e.kind != proto::EntityKind::Enemy || !(e.flags & proto::kFlagBar) || !(e.flags & proto::kFlagAlive)) continue;
    const auto s = cam.project({e.x, e.y + enemyBarY(e), e.z}, in.width, in.height);
    if (!s || glm::length(glm::dvec3(e.x, e.y, e.z) - cam.position) > 55) continue;
    const float bx = static_cast<float>(s->x) - 32, by = static_cast<float>(s->y) - 6;
    ui.rect(bx, by, 64, 6, Rgba{10, 10, 10, 180});
    ui.rect(bx, by, 64 * static_cast<float>(std::clamp(e.hpFrac, 0.0, 1.0)), 6, Rgba::hex(0xc8483a));
    ui.frame(bx - 1, by - 1, 66, 8, 1, Rgba{0, 0, 0, 200});
    if (!(e.flags & proto::kFlagAnon) && ctx.data->enemies.contains(EnemyTypeId{e.type}))
      ui.textShadow(f.bold, 11, bx + 32, by - 15, L.enemy(EnemyTypeId{e.type}), Rgba::hex(0xf0e0d0), Align::Center);
  }
  // textos flutuantes (sobem 1,6 m/s e somem depois de 0,6 s)
  for (const FloatFx& fl : fx.floats) {
    const auto s = cam.project(fl.pos + glm::dvec3(0, fl.t * 1.6, 0), in.width, in.height);
    if (!s) continue;
    const float a = static_cast<float>(1 - std::max(0.0, fl.t - 0.6) / 0.5);
    Rgba c = Rgba::hex(0xf4e6c0);
    int px = 15, face = f.bold;
    switch (fl.kind) {
      case proto::FloatKind::Crit: c = Rgba::hex(0xffe08a); px = 20; break;
      case proto::FloatKind::Hurt: c = Rgba::hex(0xff7a64); break;
      case proto::FloatKind::Heal: c = kGood; break;
      case proto::FloatKind::Block:
      case proto::FloatKind::Dodge: c = Rgba::hex(0xbfe8e2); px = 12; break;
      case proto::FloatKind::Coin: c = kGold; px = 13; break;
      default: break;
    }
    ui.textShadow(face, px, static_cast<float>(s->x), static_cast<float>(s->y) - px * 0.5f, fl.text, c.withAlpha(a), Align::Center);
  }

  return action;
}

}  // namespace rpg::client
