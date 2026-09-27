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
const Rgba kGlaze = Rgba::hex(0x46b3a8), kWarn = Rgba::hex(0xe3b24c), kBad = Rgba::hex(0xe0624a), kGood = Rgba::hex(0x7fc98a);
const Rgba kIron = Rgba::hex(0x090f15), kCobalt = Rgba::hex(0x3b7fb6), kTurb = Rgba::hex(0xa07cf2), kGold = Rgba::hex(0xf2c86a);

std::string fmt(const Localization& L, std::string_view key, const proto::MsgArgs& args) { return L.format(L.ui(key), args); }

Rgba toastColor(proto::ToastKind k) {
  switch (k) {
    case proto::ToastKind::Item: return kGlaze;
    case proto::ToastKind::Coin: return kGold;
    case proto::ToastKind::Warn: return kWarn;
    case proto::ToastKind::Danger: return kBad;
    case proto::ToastKind::Event: return kCobalt;
    case proto::ToastKind::Sky: return Rgba::hex(0xdfe8ff);
    case proto::ToastKind::Turb: return kTurb;
    case proto::ToastKind::Book: return Rgba::hex(0xdccaa2);
    default: return kMuted;
  }
}

void bar(UiBatch& ui, float x, float y, float w, float h, double frac, Rgba top, Rgba bottom) {
  ui.rect(x, y, w, h, Rgba{7, 11, 15, 230});
  const float fw = static_cast<float>(std::clamp(frac, 0.0, 1.0)) * w;
  ui.rect(x, y, fw, h * 0.5f, top);
  ui.rect(x, y + h * 0.5f, fw, h * 0.5f, bottom);
  ui.frame(x, y, w, h, 1, Rgba{34, 48, 59, 255});
}

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
  const float sw = static_cast<float>(in.width), sh = static_cast<float>(in.height);
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

  // vinheta de dano e da Turbulenta
  if (fx.hurtFlash > 0) {
    ui.rect(0, 0, sw, 40, Rgba{200, 40, 30, 70});
    ui.rect(0, sh - 40, sw, 40, Rgba{200, 40, 30, 70});
  }
  if (P->turb.inside) {
    ui.rect(0, 0, sw, 18, Rgba{90, 50, 140, 60});
    ui.rect(0, sh - 18, sw, 18, Rgba{90, 50, 140, 60});
  }

  // ---------------------------------------------------------------- vitais
  {
    const float x = 16, y = 14, pw = 272;
    drawPanel(ui, x, y, pw, 96);
    const bool anon = P->anon;
    ui.textShadow(f.display, 17, x + 13, y + 10, anon ? L.ui("hud.anon") : me->name, kBone);
    const float by = y + 40;
    bar(ui, x + 13, by, pw - 26, 15, P->maxHp > 0 ? P->hp / P->maxHp : 0, Rgba::hex(0xc8513c), Rgba::hex(0x8e2f22));
    ui.text(f.body, 10, x + 19, by + 1, fmt(L, "hud.hp", {MsgArg::integer(static_cast<long long>(std::ceil(P->hp))), MsgArg::real(P->maxHp)}), Rgba::hex(0xf3ead8));
    bar(ui, x + 13, by + 20, pw - 26, 15, P->maxVigor > 0 ? P->vigor / P->maxVigor : 0, Rgba::hex(0x5fc0b4), Rgba::hex(0x2f7f76));
    ui.text(f.body, 10, x + 19, by + 21, fmt(L, "hud.vigor", {MsgArg::integer(static_cast<long long>(std::floor(P->vigor)))}), Rgba::hex(0xf3ead8));
    float cx = x + 13;
    const auto chip = [&](std::string_view key, Rgba c) {
      const std::string t = L.ui(key);
      const float tw = ui.measure(f.bold, 10, t) + 12;
      ui.rect(cx, by + 40, tw, 16, Rgba{0, 0, 0, 80});
      ui.frame(cx, by + 40, tw, 16, 1, c);
      ui.text(f.bold, 10, cx + 6, by + 42, t, c);
      cx += tw + 4;
    };
    if (W->time - P->lastCombat < 5) chip("hud.chip.combat", kBad);
    if (P->mounted) chip("hud.chip.mounted", kGlaze);
    if (anon) chip("hud.chip.anon", kTurb);
    if (P->load == 1) chip("hud.chip.over", kWarn);
    else if (P->load == 2) chip("hud.chip.limit", kWarn);
  }

  // ---------------------------------------------------------------- faixa de zona
  {
    const auto zone = static_cast<ZoneKind>(P->zone);
    const ZoneLook& zl = ctx.look->zone(zone);
    const Rgba zc = Rgba::hex(zl.color);
    const std::string place = P->anon ? L.ui("hud.turbPlace") : L.place(static_cast<PlaceId>(P->place));
    const std::string zname = L.zone(zone);
    const float tw = ui.measure(f.body, 14, zl.mark) + ui.measure(f.bold, 11, zname) + ui.measure(f.display, 15, place) + 16 + 36;
    const float x = sw / 2 - tw / 2, y = 14;
    ui.rect(x, y, tw, 30, Rgba{17, 25, 32, 235});
    ui.frame(x, y, tw, 30, 1, kIron);
    ui.rect(x, y + 28, tw, 2, zc);
    float px = x + 18;
    px += ui.text(f.body, 14, px, y + 6, zl.mark, zc) + 8;
    px += ui.text(f.bold, 11, px, y + 9, zname, zc) + 8;
    ui.text(f.display, 15, px, y + 6, place, kBone);
    if (fx.zoneExpand > 0) {
      const float a = static_cast<float>(std::min(1.0, fx.zoneExpand / 0.3));
      const std::string rule = L.zoneRule(zone);
      const float rw = std::min(520.0f, ui.measure(f.body, 13, rule) + 26);
      const auto lines = ui.wrap(f.body, 13, rw - 26, rule);
      const float rh = 16 + 17 * static_cast<float>(lines.size());
      const float rx = sw / 2 - rw / 2, ry = y + 36;
      ui.rect(rx, ry, rw, rh, Rgba{10, 16, 21, static_cast<std::uint8_t>(225 * a)});
      ui.rect(rx, ry, 3, rh, zc.withAlpha(a));
      float ly = ry + 8;
      for (const auto& l : lines) {
        ui.text(f.body, 13, rx + 13, ly, l, kBone.withAlpha(a));
        ly += 17;
      }
    }
  }

  // ---------------------------------------------------------------- canto: relógio e widgets
  {
    const float x = sw - 16 - 196, y = 14;
    const ClockTime c = clockAt(W->time, ctx.clock);
    char hh[8];
    std::snprintf(hh, sizeof hh, "%02d", static_cast<int>(std::floor(c.hour)));
    drawPanel(ui, x, y, 196, 26);
    ui.text(f.body, 12, x + 98, y + 6, fmt(L, "hud.clock", {MsgArg::integer(c.day), MsgArg::str(hh)}), kBone, Align::Center);
    float wy = y + 32;
    const auto widget = [&](std::string_view title, const std::string& body, Rgba col, double meter, bool big) {
      const float h = big ? 64.0f : 48.0f;
      drawPanel(ui, x, wy, 196, h);
      ui.rect(x, wy, 3, h, col);
      ui.text(f.bold, 10, x + 11, wy + 8, L.ui(title), col);
      if (big) ui.text(f.bold, 22, x + 98, wy + 22, body, kBone, Align::Center);
      else ui.text(f.body, 12, x + 11, wy + 22, body, kBone);
      if (meter >= 0) {
        ui.rect(x + 11, wy + h - 12, 174, 6, Rgba{7, 11, 15, 255});
        ui.rect(x + 11, wy + h - 12, 174 * static_cast<float>(std::clamp(meter, 0.0, 1.0)), 6, col);
      }
      wy += h + 6;
    };
    if (P->turb.inside) {
      const double left = std::max(0.0, P->turb.collapseAt - W->time);
      char t[16];
      std::snprintf(t, sizeof t, "%d:%02d", static_cast<int>(left / 60), static_cast<int>(std::fmod(left, 60.0)));
      widget("hud.widget.collapse", t, kTurb, left / 240, true);
    } else if (W->portal) {
      const double left = std::max(0.0, W->portalExpires - W->time);
      widget("hud.widget.portal",
             fmt(L, "hud.widget.portalLeft", {MsgArg::ref(MsgArg::Type::Place, W->portalPlace), MsgArg::integer(static_cast<long long>(std::ceil(left)))}),
             kTurb, -1, false);
    }
    const auto place = static_cast<PlaceId>(P->place);
    if (!W->evtDone && (place == PlaceId::Passagem || place == PlaceId::Canteiro || W->evtPlayerPts > 0))
      widget("hud.widget.pass", fmt(L, "hud.widget.supply", {MsgArg::integer(static_cast<long long>(std::floor(W->evtProgress)))}), kWarn, W->evtProgress / 100, false);
  }

  // ---------------------------------------------------------------- avisos
  {
    float y = sh - 150;
    for (const ToastFx& t : fx.toasts) {
      const float a = t.left < 0 ? static_cast<float>(std::max(0.0, 1 + t.left / 0.5)) : 1.0f;
      const float tw = std::min(330.0f, sw * 0.4f);
      const auto lines = ui.wrap(f.body, 13, tw - 26, t.text);
      const float h = 18 + 17 * static_cast<float>(lines.size());
      y -= h;
      const float x = sw - 16 - tw + (1 - a) * 10;
      ui.rect(x, y, tw, h, Rgba{16, 24, 31, static_cast<std::uint8_t>(240 * a)});
      ui.frame(x, y, tw, h, 1, kIron.withAlpha(a));
      ui.rect(x, y, 3, h, toastColor(t.kind).withAlpha(a));
      float ly = y + 9;
      for (const auto& l : lines) {
        ui.text(f.body, 13, x + 13, ly, l, kBone.withAlpha(a));
        ly += 17;
      }
      y -= 6;
    }
  }

  // ---------------------------------------------------------------- prompt e canalização
  if (ctx.prompt && P->state == static_cast<std::uint8_t>(proto::PlayerState::Free)) {
    const std::string label = useLabel(*ctx.prompt, ctx);
    const float tw = ui.measure(f.body, 14, label) + 60;
    const float x = sw / 2 - tw / 2, y = sh - 142 - 34;
    drawPanel(ui, x, y, tw, 34);
    ui.rect(x + 10, y + 7, 22, 20, Rgba{40, 52, 62, 255});
    ui.frame(x + 10, y + 7, 22, 20, 1, kBronze);
    ui.text(f.bold, 12, x + 21, y + 9, "F", kBone, Align::Center);
    ui.text(f.body, 14, x + 42, y + 8, label, kBone);
  }
  if (P->channel) {
    const float x = sw / 2 - 130, y = sh - 186 - 30;
    ui.textShadow(f.bold, 12, sw / 2, y, L.message(static_cast<proto::MessageId>(P->channel->label)), kBone, Align::Center);
    bar(ui, x, y + 18, 260, 8, P->channel->dur > 0 ? P->channel->t / P->channel->dur : 0, kGlaze, Rgba::hex(0x2f7f76));
  }

  // ---------------------------------------------------------------- barra de ações
  {
    const auto* dataP = ctx.data;
    std::string fam = "punhos";
    if (P->weapon && P->weapon->cond > 0 && dataP->items.contains(ItemId{P->weapon->item})) {
      const auto& def = dataP->items[ItemId{P->weapon->item}];
      if (def.family.valid()) fam = dataP->weapons.key(def.family);
    }
    const WeaponFamilyId famId = dataP->weapons.find(fam);
    const auto& skills = dataP->weapons[famId].skills;
    const float slot = 78, gap = 5, wide = 112, weaponW = 150;
    const float total = wide + slot * 7 + gap * 8 + weaponW + 18;
    // centrada; em telas estreitas, à direita do painel de carga (que ocupa 16 + 272 px)
    float x = std::max(sw / 2 - total / 2, 16.0f + 272.0f + 10.0f);
    const float y = sh - 16 - 70;
    drawPanel(ui, x, y, total, 70);
    blockers_.push_back({x, y, total, 70});
    x += 9;
    const auto slotBox = [&](float sx, float swid, std::string_view key, const std::string& label, double cd, bool off) {
      ui.rect(sx, y + 8, swid, 54, Rgba{20, 30, 39, 255});
      ui.frame(sx, y + 8, swid, 54, 1, kIron);
      const float kw = ui.measure(f.bold, 10, key) + 8;
      ui.rect(sx + 5, y + 13, kw, 15, Rgba{40, 52, 62, 255});
      ui.text(f.bold, 10, sx + 9, y + 14, key, kBone);
      const auto lines = ui.wrap(f.body, 11, swid - 10, label);
      float ly = y + 32;
      for (std::size_t i = 0; i < std::min<std::size_t>(2, lines.size()); ++i) {
        ui.text(f.body, 11, sx + 6, ly, lines[i], off ? kMuted : kBone);
        ly += 12;
      }
      if (cd > 0) ui.rect(sx, y + 8 + 54 * static_cast<float>(1 - cd), swid, 54 * static_cast<float>(cd), Rgba{0, 0, 0, 158});
      if (off) ui.rect(sx, y + 8, swid, 54, Rgba{0, 0, 0, 110});
      const auto mx = static_cast<float>(in.mouseX), my = static_cast<float>(in.mouseY);
      return mx >= sx && mx <= sx + swid && my >= y + 8 && my <= y + 62 && in.leftPressed;
    };
    slotBox(x, wide, L.ui("hud.key.click"), fmt(L, "hud.attackUse", {MsgArg::str(L.ui(fam == "arco" ? "hud.shoot" : "hud.attack"))}), 0, false);
    x += wide + gap;
    slotBox(x, slot, L.ui("hud.key.right"), L.ui("hud.aim"), 0, false);
    x += slot + gap;
    slotBox(x, slot, L.ui("hud.key.space"), L.ui("hud.jump"), 0, false);
    x += slot + gap;
    slotBox(x, slot, "C", L.ui("hud.dodge"), 0, false);
    x += slot + gap;
    for (int i = 0; i < 2; ++i) {
      const bool has = static_cast<std::size_t>(i) < skills.size() && skills[static_cast<std::size_t>(i)].valid();
      const double cdv = i == 0 ? P->cdQ : P->cdE;
      double frac = 0;
      std::string name = "—";
      if (has) {
        const SkillId sid = skills[static_cast<std::size_t>(i)];
        name = L.skill(sid);
        const double cooldown = dataP->skills[sid].cooldown;
        frac = cooldown > 0 ? cdv / cooldown : 0;
      }
      slotBox(x, slot, i == 0 ? "Q" : "E", name, frac, !has);
      x += slot + gap;
    }
    int potions = 0;
    const ItemId pocao = dataP->items.find("pocao");
    for (const auto& e : P->inv)
      if (e.item == pocao.value) potions += e.qty;
    if (slotBox(x, slot, "1", L.ui("hud.potion") + " ×" + std::to_string(potions), P->cdPot / 2, false)) action = Action::Potion;
    x += slot + gap;
    std::string mountLabel = L.ui("hud.mount.none");
    if (P->mount) {
      if (P->mounted) mountLabel = L.ui("hud.mount.dismount");
      else if (P->mountPresent && std::hypot(P->mountX - me->x, P->mountZ - me->z) < 5) mountLabel = L.ui("hud.mount.mount");
      else mountLabel = L.ui("hud.mount.call");
    }
    if (slotBox(x, slot, "R", mountLabel, 0, !P->mount)) action = Action::Mount;
    x += slot + gap;
    // arma
    ui.text(f.body, 10, x + 4, y + 10, L.ui("hud.weapon"), kMuted);
    if (P->weapon) {
      const double cond = P->weapon->cond;
      ui.text(f.bold, 12, x + 4, y + 23, L.item(ItemId{P->weapon->item}), kBone);
      ui.rect(x + 4, y + 40, weaponW - 8, 5, Rgba{7, 11, 15, 255});
      ui.rect(x + 4, y + 40, (weaponW - 8) * static_cast<float>(cond / 100), 5, cond < 35 ? kBad : cond < 70 ? kWarn : kGood);
      ui.text(f.body, 10, x + 4, y + 49, fmt(L, "hud.weapon.cond", {MsgArg::integer(static_cast<long long>(std::round(cond)))}), kMuted);
    } else {
      ui.text(f.bold, 12, x + 4, y + 23, L.ui("hud.weapon.none"), kBone);
      ui.text(f.body, 10, x + 4, y + 42, L.ui("hud.weapon.kit"), kMuted);
    }
  }

  // ---------------------------------------------------------------- carga
  {
    const float x = 16, pw = 272, h = P->saddleCapacity > 0 ? 104.0f : 84.0f, y = sh - 16 - h;
    drawPanel(ui, x, y, pw, h);
    const float cw = ui.text(f.bold, 16, x + 13, y + 10, Localization::number(P->coins), kGold);
    ui.text(f.body, 12, x + 13 + cw + 6, y + 13, L.ui("hud.coinsWord"), kMuted);
    const auto row = [&](float ry, std::string_view label, double wgt, double cap, double scale, Rgba col) {
      ui.text(f.body, 12, x + 13, ry, L.ui(label), kBone);
      bar(ui, x + 76, ry + 2, 100, 10, cap > 0 ? wgt / (cap * scale) : 0, col, col);
      char t[48];
      std::snprintf(t, sizeof t, "%.1f / %s kg", wgt, Localization::number(cap).c_str());
      ui.text(f.body, 11, x + 184, ry + 1, t, kBone);
    };
    row(y + 36, "hud.bag", P->bagWeight, P->capacity, 1.3, kGlaze);
    float ly = y + 56;
    if (P->saddleCapacity > 0) {
      row(ly, "hud.saddle", P->saddleWeight, P->saddleCapacity, 1.0, kCobalt);
      ly += 20;
    }
    const std::string_view ls = P->load == 1 ? "hud.load.over" : P->load == 2 ? "hud.load.limit" : "hud.load.normal";
    ui.text(f.body, 11, x + 13, ly, L.ui(ls), P->load ? kWarn : kMuted);
  }

  if (ctx.showKeys) {
    const std::string keys = L.ui("hud.keys");
    ui.textShadow(f.body, 11, sw / 2, sh - 108 - 14, keys, kMuted, Align::Center);
  }

  // ---------------------------------------------------------------- mira e marcador de acerto
  if (ctx.aiming) {
    const float mx = static_cast<float>(in.mouseX), my = static_cast<float>(in.mouseY);
    const bool on = ctx.aimEnemy != 0 || (ctx.hover && ctx.hover->kind == Hover::Kind::Enemy);
    const Rgba c = on ? Rgba::hex(0xff4828) : Rgba::hex(0xf2ead9);
    const float r = on ? 14 : 16;
    constexpr int kSeg = 32;
    for (int i = 0; i < kSeg; ++i) {
      const float a = 6.2831853f * static_cast<float>(i) / kSeg;
      ui.rect(mx + std::cos(a) * r - 0.75f, my + std::sin(a) * r - 0.75f, 1.5f, 1.5f, c);
    }
    ui.rect(mx - 2, my - 2, 4, 4, on ? c : Rgba::hex(0xffffff));
    ui.rect(mx - 1, my - 25, 2, 8, c);
    ui.rect(mx - 1, my + 17, 2, 8, c);
    ui.rect(mx - 25, my - 1, 8, 2, c);
    ui.rect(mx + 17, my - 1, 8, 2, c);
    if (on && ctx.aimDist > 0) ui.textShadow(f.bold, 11, mx + 16, my + 11, std::to_string(static_cast<long long>(std::round(ctx.aimDist))) + "m", c);
    if (fx.hitmarker > 0) {
      const Rgba hc = fx.hitmarkerCrit ? Rgba::hex(0xff3322) : Rgba::hex(0xffffff);
      for (int q = 0; q < 4; ++q) {
        const float sx = q & 1 ? 1.0f : -1.0f, sy = q & 2 ? 1.0f : -1.0f;
        for (int k = 0; k < 6; ++k) ui.rect(mx + sx * (5 + k) - 1, my + sy * (5 + k) - 1, 2, 2, hc);
      }
    }
  }

  // ---------------------------------------------------------------- derrubado
  if (P->state == static_cast<std::uint8_t>(proto::PlayerState::Down)) {
    bool guard = false;
    for (const auto& e : w.entities())
      if (e.kind == proto::EntityKind::Enemy && (e.flags & proto::kFlagAlive) && ctx.data->enemies.contains(EnemyTypeId{e.type}) &&
          ctx.data->enemies[EnemyTypeId{e.type}].faction == Faction::Guard && std::hypot(e.x - me->x, e.z - me->z) < 24)
        guard = true;
    const float y = sh * 0.38f - 40;
    ui.textShadow(f.display, 34, sw / 2, y, L.ui("hud.down"), Rgba::hex(0xff8a70), Align::Center);
    ui.textShadow(f.body, 14, sw / 2, y + 48, L.ui(guard ? "hud.down.guard" : "hud.down.alone"), kBone, Align::Center);
    bar(ui, sw / 2 - 130, y + 74, 260, 8, P->stateT / (guard ? 2.5 : 4.5), kBad, Rgba::hex(0x8e2f22));
  }

  // ---------------------------------------------------------------- relatório de derrota
  if (fx.death && fx.deathDelay <= 0) {
    const proto::DeathReport& r = *fx.death;
    const auto zone = static_cast<ZoneKind>(r.zone);
    const ZoneLook& zl = ctx.look->zone(zone);
    const float pw = std::min(760.0f, sw - 40), ph = 420, x = sw / 2 - pw / 2, y = sh / 2 - ph / 2;
    ui.rect(0, 0, sw, sh, Rgba{0, 0, 0, 120});
    drawPanel(ui, x, y, pw, ph);
    blockers_.push_back({0, 0, sw, sh});
    ui.text(f.display, 24, x + 24, y + 20, fmt(L, "death.title", {MsgArg::ref(MsgArg::Type::Place, r.place)}), kBone);
    const std::string tag = zl.mark + " " + L.zone(zone);
    const float tw = ui.text(f.bold, 13, x + 24, y + 58, tag, Rgba::hex(zl.color));
    ui.paragraph(f.body, 13, x + 24 + tw + 10, y + 58, pw - tw - 60, L.zoneDeath(zone), kBone);
    ui.rect(x + 24, y + 96, pw - 48, 1, Rgba{165, 132, 79, 110});
    const float colW = (pw - 72) / 2;
    float ly = y + 110;
    const auto list = [&](float cx, float& cy, const std::vector<proto::DeathLine>& lines) {
      if (lines.empty()) {
        ui.text(f.body, 13, cx, cy, L.ui("death.nothing"), kMuted);
        cy += 20;
        return;
      }
      for (const auto& l : lines) {
        std::string s = (l.qty > 1 ? std::to_string(l.qty) + "× " : std::string{}) + L.item(ItemId{l.item});
        ui.text(f.body, 13, cx + 10, cy, "• " + s, kBone);
        cy += 18;
      }
    };
    ui.text(f.bold, 14, x + 24, ly, L.ui("death.lost"), kBronze);
    ly += 24;
    list(x + 24, ly, r.lost);
    if (!r.destroyed.empty()) {
      ly += 6;
      ui.text(f.bold, 14, x + 24, ly, L.ui("death.destroyed"), kBronze);
      ly += 24;
      list(x + 24, ly, r.destroyed);
    }
    if (r.bag && zone != ZoneKind::Turbulenta) ui.paragraph(f.body, 12, x + 24, ly + 8, colW, L.ui("death.bag"), kMuted);
    float ry = y + 110;
    const float rx = x + 48 + colW;
    ui.text(f.bold, 14, rx, ry, L.ui("death.kept"), kBronze);
    ry += 24;
    for (const MsgArg& k : r.kept) {
      std::string s;
      if (k.type == MsgArg::Type::Message && static_cast<proto::MessageId>(k.id) == proto::MessageId::KeptCoins)
        s = L.message(proto::MessageId::KeptCoins, {MsgArg::integer(r.coinsKept)});
      else
        s = L.arg(k);
      ui.text(f.body, 13, rx + 10, ry, "• " + s, kBone);
      ry += 18;
    }
    if (!r.hasWeapon) ui.paragraph(f.body, 12, rx, ry + 8, colW, L.ui("death.noWeapon"), kWarn);
    if (drawButton(ui, f, in, x + pw - 24 - 260, y + ph - 58, 260, 38, L.ui("death.respawn"), true) || in.hit(Key::Enter))
      action = Action::Respawn;
  }
  return action;
}

}  // namespace rpg::client
