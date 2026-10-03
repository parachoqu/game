// HUD do documento game.rml: porte de ui/hud.js (vitais, faixa de zona, minimapa, relógio, widgets,
// avisos, prompt, canalização, barra de ações, carga, mira, marcador de acerto, derrubado, vinheta).
#include <algorithm>
#include <cmath>
#include <format>

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include "client/ClientWorld.h"
#include "client/Presentation.h"
#include "client/fx/FxState.h"
#include "client/ui/GameUi.h"
#include "client/ui/Localization.h"
#include "core/GameClock.h"
#include "core/protocol/Replicated.h"

namespace rpg::client {

namespace proto = protocol;
using proto::MsgArg;

namespace {

std::string pct(double v) { return std::format("{:.2f}%", std::clamp(v, 0.0, 100.0)); }

const char* toastClass(proto::ToastKind k) {
  switch (k) {
    case proto::ToastKind::Item: return "item";
    case proto::ToastKind::Coin: return "coin";
    case proto::ToastKind::Warn: return "warn";
    case proto::ToastKind::Danger: return "danger";
    case proto::ToastKind::Event: return "event";
    case proto::ToastKind::Sky: return "sky";
    case proto::ToastKind::Turb: return "turb";
    case proto::ToastKind::Book: return "book";
    default: return "info";
  }
}

}  // namespace

void GameUi::showHud(bool on) {
  if (hudOn_ == on) return;
  hudOn_ = on;
  UiSystem::setHidden(byId("hud"), !on);
  if (!on) {
    UiSystem::setHidden(byId("crosshair"), true);
    UiSystem::setHidden(byId("down"), true);
    UiSystem::setClass(byId("vignette"), "hit", false);
    UiSystem::setClass(byId("vignette"), "turb", false);
  }
}

void GameUi::toggleMinimap() {
  // M (ou o botão +) alterna entre o tamanho normal e o ampliado; o minimapa nunca some
  miniBig_ = !miniBig_;
  UiSystem::setClass(byId("corner"), "big", miniBig_);
  UiSystem::setText(byId("mini-size"), miniBig_ ? "−" : "+");
  miniT_ = 0;
}

void GameUi::caption(const std::pair<std::string, std::string>& text) {
  UiSystem::setHidden(byId("caption"), text.first.empty());
  UiSystem::setText(byId("cap-title"), text.first);
  UiSystem::setText(byId("cap-text"), text.second);
}

void GameUi::syncToasts(const FxState& fx) {
  Rml::Element* box = byId("toasts");
  if (!box) return;
  // some os que saíram da fila
  for (auto it = toastEls_.begin(); it != toastEls_.end();) {
    const bool alive = std::any_of(fx.toasts.begin(), fx.toasts.end(), [&](const ToastFx& t) { return t.id == it->first; });
    if (!alive) {
      box->RemoveChild(it->second);
      it = toastEls_.erase(it);
    } else {
      ++it;
    }
  }
  // novos entram no começo (hud.js `box.prepend`); o mais novo é o primeiro da fila
  for (auto t = fx.toasts.rbegin(); t != fx.toasts.rend(); ++t) {
    const auto found = std::find_if(toastEls_.begin(), toastEls_.end(), [&](const auto& p) { return p.first == t->id; });
    Rml::Element* el = found != toastEls_.end() ? found->second : nullptr;
    if (!el) {
      Rml::ElementPtr fresh = doc_->CreateElement("div");
      el = box->InsertBefore(std::move(fresh), box->GetFirstChild());
      el->SetClassNames(std::string("toast ") + toastClass(t->kind));
      UiSystem::setInner(el, UiSystem::escape(t->text));
      toastEls_.emplace_back(t->id, el);
    }
    UiSystem::setClass(el, "out", t->left < 0);
  }
}

void GameUi::drawMinimap(const HudInput& in) {
  const ClientWorld& w = *in.world;
  const proto::PrivateState* P = w.self();
  const proto::EntityState* me = w.me();
  const proto::WorldState* W = w.world();
  if (!P || !me || !W || !map_.built()) return;
  MapMarkers m;
  m.playerX = me->x;
  m.playerZ = me->z;
  m.playerYaw = me->yaw;
  m.camYaw = in.camYaw;
  m.turbulent = static_cast<MapKind>(me->map) == MapKind::Turbulent;
  m.time = W->time;
  for (const auto& e : w.entities())
    if (e.kind == proto::EntityKind::LootBag && (e.flags & proto::kFlagOwn) && static_cast<MapKind>(e.map) == MapKind::Region)
      m.ownBags.emplace_back(e.x, e.z);
  m.portal = W->portal;
  m.portalX = W->portalX;
  m.portalZ = W->portalZ;
  m.mount = P->mountPresent && !P->mounted;
  m.mountX = P->mountX;
  m.mountZ = P->mountZ;
  const int size = miniBig_ ? 320 : 176;
  if (mini_.width() != size) mini_ = Raster(size, size);
  map_.drawMinimap(mini_, m, miniBig_);
  ui_.render().setDynamicTexture("dyn:minimap", size, size, mini_.pixels());
  UiSystem::setHidden(byId("mini-noref"), !m.turbulent);
  // nomes dos lugares só no minimapa ampliado (map.js `markers(…, full)`)
  std::string labels;
  if (miniBig_ && !m.turbulent) {
    double scale = 1, ox = 0, oz = 0;
    map_.minimapFrame(m.playerX, m.playerZ, size, true, scale, ox, oz);
    for (const MapLabel& l : map_.labels(scale, ox, oz, [&](const std::string& k) { return P->flags.contains("disc_" + k); })) {
      if (l.x < -60 || l.y < -10 || l.x > size + 60 || l.y > size + 20) continue;
      labels += std::format(R"(<span class="maplabel{}" style="left: {:.0f}px; top: {:.0f}px;">{}</span>)", l.known ? "" : " unknown", l.x,
                            l.y - 20, l.known ? UiSystem::escape(L_.ui("map.place." + l.key)) : "?");
    }
  }
  UiSystem::setInner(byId("mini-labels"), labels);
}

std::vector<GameUi::HudAction> GameUi::hud(const HudInput& in) {
  std::vector<HudAction> acts;
  acts.swap(hudActs_);
  const ClientWorld& w = *in.world;
  const FxState& fx = *in.fx;
  const proto::PrivateState* P = w.self();
  const proto::EntityState* me = w.me();
  const proto::WorldState* W = w.world();
  if (!P || !me || !W || !hudOn_) return acts;
  const auto zone = static_cast<ZoneKind>(P->zone);
  const ZoneLook& zl = look_.zone(zone);

  // barras (todo quadro)
  UiSystem::setStyle(byId("hp-fill"), "width", pct(P->maxHp > 0 ? P->hp / P->maxHp * 100 : 0));
  UiSystem::setStyle(byId("vg-fill"), "width", pct(P->maxVigor > 0 ? P->vigor / P->maxVigor * 100 : 0));
  UiSystem::setStyle(byId("load-fill"), "width", pct(P->capacity > 0 ? P->bagWeight / (P->capacity * 1.3) * 100 : 0));
  if (P->channel) {
    UiSystem::setHidden(byId("channel"), false);
    UiSystem::setText(byId("channel-label"), L_.message(static_cast<proto::MessageId>(P->channel->label)));
    UiSystem::setStyle(byId("channel-fill"), "width", pct(P->channel->dur > 0 ? P->channel->t / P->channel->dur * 100 : 0));
  } else {
    UiSystem::setHidden(byId("channel"), true);
  }

  // mira: segue o cursor e fica vermelha sobre um inimigo
  const bool aiming = in.aiming && !in.uiOpen;
  Rml::Element* cross = byId("crosshair");
  UiSystem::setHidden(cross, !aiming);
  if (aiming) {
    UiSystem::setStyle(cross, "left", std::format("{:.0f}px", in.mouseX));
    UiSystem::setStyle(cross, "top", std::format("{:.0f}px", in.mouseY));
    UiSystem::setClass(cross, "on", in.aimOnTarget);
    UiSystem::setText(byId("cross-range"), in.aimOnTarget && in.aimDist > 0 ? std::format("{}m", std::lround(in.aimDist)) : "");
  }
  Rml::Element* hm = byId("hitmarker");
  UiSystem::setClass(hm, "active", fx.hitmarker > 0);
  UiSystem::setClass(hm, "crit", fx.hitmarker > 0 && fx.hitmarkerCrit);

  // derrubado
  const bool downNow = P->state == static_cast<std::uint8_t>(proto::PlayerState::Down);
  UiSystem::setHidden(byId("down"), !downNow);
  if (downNow) {
    bool guard = false;
    for (const auto& e : w.entities())
      if (e.kind == proto::EntityKind::Enemy && (e.flags & proto::kFlagAlive) && data_.enemies.contains(EnemyTypeId{e.type}) &&
          data_.enemies[EnemyTypeId{e.type}].faction == Faction::Guard && std::hypot(e.x - me->x, e.z - me->z) < 24)
        guard = true;
    UiSystem::setText(byId("down-text"), L_.ui(guard ? "hud.down.guard" : "hud.down.alone"));
    UiSystem::setStyle(byId("down-fill"), "width", pct(P->stateT / (guard ? 2.5 : 4.5) * 100));
  }

  // vinheta de dano e da Turbulenta; faixa de zona
  UiSystem::setClass(byId("vignette"), "hit", fx.hurtFlash > 0);
  UiSystem::setClass(byId("vignette"), "turb", zone == ZoneKind::Turbulenta);
  {
    const std::string zc = std::format("#{:06x}", zl.color & 0xFFFFFF);
    UiSystem::setStyle(byId("zone-mark"), "color", zc);
    UiSystem::setStyle(byId("zone-name"), "color", zc);
    UiSystem::setStyle(byId("zone-line"), "border-bottom-color", zc);
    UiSystem::setStyle(byId("zone-rule"), "border-left-color", zc);
    UiSystem::setText(byId("zone-mark"), zl.mark);
    UiSystem::setText(byId("zone-name"), L_.zone(zone));
    UiSystem::setText(byId("zone-place"), P->anon ? L_.ui("hud.turbPlace") : L_.place(static_cast<PlaceId>(P->place)));
    UiSystem::setText(byId("zone-rule"), L_.zoneRule(zone));
    UiSystem::setClass(byId("zone"), "expanded", fx.zoneExpand > 0);
  }
  UiSystem::setClass(doc_, "cine", P->cinematic);

  // prompt (interact.js)
  UiSystem::setHidden(byId("prompt"), in.prompt.empty());
  if (!in.prompt.empty()) UiSystem::setInner(byId("prompt"), in.prompt);

  syncToasts(fx);

  // minimapa (10 Hz) e o botão do norte
  miniT_ -= in.dt;
  if (miniT_ <= 0) {
    miniT_ = 0.1;
    drawMinimap(in);
    const bool turned = std::abs(std::atan2(std::sin(in.camYaw), std::cos(in.camYaw))) > 0.05 || std::abs(in.camPitch - 0.67) > 0.05;
    UiSystem::setClass(byId("cam-north"), "turned", turned);
    UiSystem::setText(byId("cam-north"), turned ? "N ↺" : "N");
  }

  textT_ -= in.dt;
  if (textT_ > 0) return acts;
  textT_ = 0.12;
  UiSystem::setText(byId("hud-name"), P->anon ? L_.ui("hud.anon") : me->name);
  std::string title;
  if (!P->anon && fx.shownTitle)
    for (const auto& t : fx.titles)
      if (t.id == *fx.shownTitle) title = L_.bookTitleName(t);
  UiSystem::setText(byId("hud-title"), title);
  UiSystem::setText(byId("hp-text"), L_.format(L_.ui("hud.hp"), {MsgArg::integer(static_cast<long long>(std::ceil(P->hp))), MsgArg::real(P->maxHp)}));
  UiSystem::setText(byId("vg-text"), L_.format(L_.ui("hud.vigor"), {MsgArg::integer(static_cast<long long>(std::floor(P->vigor)))}));
  std::string chips;
  const auto chip = [&](const char* cls, std::string_view key) { chips += std::format(R"(<span class="chip {}">{}</span>)", cls, UiSystem::escape(L_.ui(key))); };
  if (W->time - P->lastCombat < 5) chip("combat", "hud.chip.combat");
  if (P->mounted) chip("mount", "hud.chip.mounted");
  if (P->anon) chip("anon", "hud.chip.anon");
  if (P->load == 1) chip("over", "hud.chip.over");
  else if (P->load == 2) chip("over", "hud.chip.limit");
  UiSystem::setInner(byId("chips"), chips);

  const ClockTime c = clockAt(W->time, ClockConfig{});
  UiSystem::setText(byId("clock"), L_.format(L_.ui("hud.clock"), {MsgArg::integer(c.day), MsgArg::str(std::format("{:02d}", static_cast<int>(std::floor(c.hour))))}));

  // ações
  std::string fam = "punhos";
  if (P->weapon && P->weapon->cond > 0 && data_.items.contains(ItemId{P->weapon->item})) {
    const auto& def = data_.items[ItemId{P->weapon->item}];
    if (def.family.valid()) fam = data_.weapons.key(def.family);
  }
  UiSystem::setText(byId("ab-basic"), L_.ui(fam == "arco" ? "hud.shoot" : "hud.attack"));
  std::string mountLabel = L_.ui("hud.mount.none");
  if (P->mount) {
    if (P->mounted) mountLabel = L_.ui("hud.mount.dismount");
    else if (P->mountPresent && std::hypot(P->mountX - me->x, P->mountZ - me->z) < 5) mountLabel = L_.ui("hud.mount.mount");
    else mountLabel = L_.ui("hud.mount.call");
  }
  UiSystem::setText(byId("ab-mount-label"), mountLabel);
  UiSystem::setClass(byId("ab-mount"), "off", !P->mount);
  const auto& skills = data_.weapons[data_.weapons.find(fam)].skills;
  for (int i = 0; i < 2; ++i) {
    const bool has = static_cast<std::size_t>(i) < skills.size() && skills[static_cast<std::size_t>(i)].valid();
    Rml::Element* slot = byId(i == 0 ? "ab-q" : "ab-e");
    UiSystem::setClass(slot, "off", !has);
    UiSystem::setText(byId(i == 0 ? "ab-q-name" : "ab-e-name"), has ? L_.skill(skills[static_cast<std::size_t>(i)]) : "—");
    double frac = 0;
    if (has) {
      const double cd = data_.skills[skills[static_cast<std::size_t>(i)]].cooldown;
      frac = cd > 0 ? (i == 0 ? P->cdQ : P->cdE) / cd : 0;
    }
    UiSystem::setStyle(byId(i == 0 ? "ab-q-cd" : "ab-e-cd"), "height", pct(frac * 100));
  }
  int potions = 0;
  const ItemId pocao = data_.items.find("pocao");
  for (const auto& e : P->inv)
    if (e.item == pocao.value) potions += e.qty;
  UiSystem::setText(byId("ab-pot-count"), std::format("×{}", potions));
  UiSystem::setStyle(byId("ab-pot-cd"), "height", pct(P->cdPot / 2 * 100));
  std::string weapon;
  if (P->weapon) {
    const double cond = P->weapon->cond;
    weapon = std::format(R"(<small>{}</small><b>{}</b><div class="cond"><i class="{}" style="width: {}"></i></div><small>{}</small>)",
                         UiSystem::escape(L_.ui("hud.weapon")), UiSystem::escape(L_.item(ItemId{P->weapon->item})),
                         cond < 35 ? "low" : cond < 70 ? "mid" : "", pct(cond),
                         UiSystem::escape(L_.format(L_.ui("hud.weapon.cond"), {MsgArg::integer(std::lround(cond))})));
  } else {
    weapon = std::format("<small>{}</small><b>{}</b><small>{}</small>", UiSystem::escape(L_.ui("hud.weapon")),
                         UiSystem::escape(L_.ui("hud.weapon.none")), UiSystem::escape(L_.ui("hud.weapon.kit")));
  }
  UiSystem::setInner(byId("ab-weapon"), weapon);

  // carga
  UiSystem::setText(byId("coins"), Localization::number(P->coins));
  UiSystem::setText(byId("load-text"), std::format("{:.1f} / {} kg", P->bagWeight, Localization::number(P->capacity)));
  UiSystem::setHidden(byId("saddle-row"), P->saddleCapacity <= 0);
  if (P->saddleCapacity > 0) {
    UiSystem::setStyle(byId("saddle-fill"), "width", pct(P->saddleWeight / P->saddleCapacity * 100));
    UiSystem::setText(byId("saddle-text"), std::format("{:.1f} / {} kg", P->saddleWeight, Localization::number(P->saddleCapacity)));
  }
  Rml::Element* ls = byId("load-state");
  UiSystem::setClass(ls, "over", P->load == 1);
  UiSystem::setClass(ls, "limit", P->load == 2);
  UiSystem::setText(ls, L_.ui(P->load == 1 ? "hud.load.over" : P->load == 2 ? "hud.load.limit" : "hud.load.normal"));

  // widgets de contexto
  std::string wd;
  if (P->turb.inside) {
    const double left = std::max(0.0, P->turb.collapseAt - W->time);
    wd += std::format(R"(<div class="widget turb"><h4>{}</h4><div class="big">{}:{:02d}</div><div class="meter"><i style="width: {}"></i></div></div>)",
                      UiSystem::escape(L_.ui("hud.widget.collapse")), static_cast<int>(left / 60), static_cast<int>(std::fmod(left, 60.0)),
                      pct(left / 240 * 100));
  } else if (W->portal) {
    const double left = std::max(0.0, W->portalExpires - W->time);
    wd += std::format(R"(<div class="widget turb"><h4>{}</h4>{}</div>)", UiSystem::escape(L_.ui("hud.widget.portal")),
                      UiSystem::escape(L_.format(L_.ui("hud.widget.portalLeft"),
                                                 {MsgArg::ref(MsgArg::Type::Place, W->portalPlace), MsgArg::integer(static_cast<long long>(std::ceil(left)))})));
  }
  const auto place = static_cast<PlaceId>(P->place);
  if (!W->evtDone && (place == PlaceId::Passagem || place == PlaceId::Canteiro || W->evtPlayerPts > 0))
    wd += std::format(R"(<div class="widget front"><h4>{}</h4>{}<div class="meter"><i style="width: {}"></i></div></div>)",
                      UiSystem::escape(L_.ui("hud.widget.pass")),
                      UiSystem::escape(L_.format(L_.ui("hud.widget.supply"), {MsgArg::integer(static_cast<long long>(std::floor(W->evtProgress)))})),
                      pct(W->evtProgress));
  UiSystem::setInner(byId("widgets"), wd);
  return acts;
}

}  // namespace rpg::client
