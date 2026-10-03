#include "client/ui/GameUi.h"

#include <algorithm>
#include <cmath>
#include <format>

#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>

#include "client/Presentation.h"
#include "client/ui/Localization.h"

namespace rpg::client {

const std::vector<std::string> kSuggestedNames = {"Maren", "Ilo", "Tessaly", "Ravi", "Oriane", "Kael", "Nadja", "Bento", "Suri", "Aurel"};
const std::vector<std::string> kCharacterModels = {"paladina", "kachujin", "eve"};

namespace {

std::string hex(std::uint32_t rgb) { return std::format("#{:06x}", rgb & 0xFFFFFF); }

}  // namespace

GameUi::GameUi(UiSystem& ui, const Localization& L, const GameData& data, const Presentation& look)
    : ui_(ui), L_(L), data_(data), look_(look) {
  // texturas dinâmicas usadas pelo documento (preenchidas depois pelo jogo)
  ui_.render().setDynamicTexture("dyn:minimap", 1, 1, {0, 0, 0, 0});
  ui_.render().setDynamicTexture("dyn:fullmap", 1, 1, {0, 0, 0, 0});
  doc_ = ui_.load("game.rml");
  doc_->Show();
  ui_.onAction([this](const std::string& act, const std::string& arg, Rml::Element*) { onAction(act, arg); });
  // as regras das zonas, como a tela de carregamento da demo ensina enquanto monta
  std::string lore;
  for (ZoneKind z : {ZoneKind::Protegida, ZoneKind::Fronteira, ZoneKind::FullLoot, ZoneKind::Turbulenta}) {
    const ZoneLook& zl = look_.zone(z);
    lore += std::format(R"(<p><b style="color: {}">{} Zona {}</b>{}</p>)", hex(zl.color), UiSystem::escape(zl.mark),
                        UiSystem::escape(L_.zone(z)), UiSystem::escape(L_.zoneRule(z)));
  }
  UiSystem::setInner(byId("boot-lore"), lore);
}

Rml::Element* GameUi::byId(const char* id) { return doc_->GetElementById(id); }

// ---------------------------------------------------------------- carregamento

void GameUi::setBootPhases(std::vector<BootPhase> phases) {
  phases_ = std::move(phases);
  std::string li, marks;
  double acc = 0;
  for (std::size_t i = 0; i < phases_.size(); ++i) {
    li += std::format(R"(<li><span class="dot">◆</span>{}</li>)", UiSystem::escape(phases_[i].label));
    if (i + 1 < phases_.size()) {
      acc += phases_[i].weight;
      marks += std::format(R"(<em class="mk" style="left: {:.1f}%"></em>)", acc * 100);
    }
  }
  UiSystem::setInner(byId("boot-phases"), li);
  // marcas nas fronteiras entre as fases (main.js), por cima do preenchimento
  if (Rml::Element* track = byId("boot-track"))
    track->SetInnerRML(R"(<i id="boot-fill"><i class="ember"></i></i><i class="sheen"></i>)" + marks);
}

void GameUi::bootProgress(int phase, std::string_view sub, int subs) {
  double base = 0;
  for (int i = 0; i < phase && i < static_cast<int>(phases_.size()); ++i) base += phases_[static_cast<std::size_t>(i)].weight;
  // dentro da fase a barra avança por aproximação: cada sub-etapa cobre parte do que falta
  const double creep = phase >= 0 && phase < static_cast<int>(phases_.size())
                           ? phases_[static_cast<std::size_t>(phase)].weight * (1 - std::pow(0.86, subs)) * 0.92
                           : 0.0;
  const std::string label = phase >= 0 && phase < static_cast<int>(phases_.size()) ? phases_[static_cast<std::size_t>(phase)].label : "";
  UiSystem::setText(byId("boot-text"), sub.empty() ? label + "…" : label + " — " + std::string(sub));
  UiSystem::setStyle(byId("boot-fill"), "width", std::format("{}%", std::min(100L, std::lround((base + creep) * 100))));
  if (Rml::Element* list = byId("boot-phases"))
    for (int i = 0; i < list->GetNumChildren(); ++i) {
      Rml::Element* li = list->GetChild(i);
      UiSystem::setClass(li, "done", i < phase);
      UiSystem::setClass(li, "now", i == phase);
    }
}

void GameUi::bootDone() {
  UiSystem::setStyle(byId("boot-fill"), "width", "100%");
  if (Rml::Element* list = byId("boot-phases"))
    for (int i = 0; i < list->GetNumChildren(); ++i) {
      UiSystem::setClass(list->GetChild(i), "done", true);
      UiSystem::setClass(list->GetChild(i), "now", false);
    }
}

void GameUi::bootLore(double time) {
  // main.js: cada parágrafo aparece por ~4 s a cada 18 s, defasado em 4,5 s
  Rml::Element* box = byId("boot-lore");
  if (!box || box->GetNumChildren() == 0) return;
  const int n = box->GetNumChildren();
  const int cur = static_cast<int>(std::fmod(time / 4.5, static_cast<double>(n)));
  if (cur == lore_) return;
  lore_ = cur;
  for (int i = 0; i < n; ++i) UiSystem::setClass(box->GetChild(i), "on", i == cur);
}

// ---------------------------------------------------------------- título e criação

void GameUi::showTitle(bool canContinue) {
  UiSystem::setHidden(byId("screen-loading"), true);
  UiSystem::setHidden(byId("screen-create"), true);
  UiSystem::setHidden(byId("screen-title"), false);
  UiSystem::setHidden(byId("btn-continue"), !canContinue);
  form_ = nullptr;
}

void GameUi::showCreate(CreationForm& form) {
  form_ = &form;
  UiSystem::setHidden(byId("screen-title"), true);
  UiSystem::setHidden(byId("screen-create"), false);
  if (auto* name = dynamic_cast<Rml::ElementFormControlInput*>(byId("c-name"))) {
    name->SetValue(form.name);
    name->Focus();
  }
  renderCards(form);
}

void GameUi::hideScreens() {
  for (const char* id : {"screen-loading", "screen-title", "screen-create"}) UiSystem::setHidden(byId(id), true);
  form_ = nullptr;
}

void GameUi::renderCards(CreationForm& form) {
  const auto card = [](const std::string& act, std::size_t i, bool on, const std::string& title, const std::string& desc,
                       const std::string& swatch = {}) {
    return std::format(R"(<div class="card{}" data-act="{}" data-arg="{}"><span class="mark">◆</span>{}<b>{}</b><span>{}</span></div>)",
                       on ? " checked" : "", act, i, swatch, UiSystem::escape(title), UiSystem::escape(desc));
  };
  std::string models, origins, starts;
  for (std::size_t i = 0; i < kCharacterModels.size(); ++i) {
    const std::string& m = kCharacterModels[i];
    models += card("pickModel", i, form.model == i, L_.ui("model." + m + ".name"), L_.ui("model." + m + ".desc"));
  }
  for (std::size_t i = 0; i < data_.origins.size(); ++i) {
    const OriginId id{static_cast<std::uint16_t>(i)};
    const OriginLook& o = look_.origin(data_.origins.key(id));
    const std::string sw =
        std::format(R"x(<div class="sw" style="decorator: linear-gradient(90deg, {0} 0%, {0} 50%, {1} 50%, {1} 100%)"></div>)x", hex(o.skin), hex(o.cloth));
    origins += card("pickOrigin", i, form.origin == i, L_.origin(id), L_.originHint(id), sw);
  }
  for (std::size_t i = 0; i < data_.starts.size(); ++i) {
    const StartId id{static_cast<std::uint16_t>(i)};
    starts += card("pickStart", i, form.start == i, L_.start(id), L_.startDesc(id));
  }
  UiSystem::setInner(byId("c-models"), models);
  UiSystem::setInner(byId("c-origins"), origins);
  UiSystem::setInner(byId("c-starts"), starts);
}

void GameUi::onAction(const std::string& act, const std::string& arg) {
  if (act.rfind("p:", 0) == 0) return panelAction(act.substr(2), arg);
  if (act.rfind("bk:", 0) == 0) return bookAction(act.substr(3), arg);
  if (act.rfind("tab:", 0) == 0) return openMenu(act.substr(4));
  const auto index = [&] { return static_cast<std::size_t>(std::max(0, std::atoi(arg.c_str()))); };
  if (act == "titleNew") pending_ = ScreenAction::NewGame;
  else if (act == "titleContinue") pending_ = ScreenAction::Continue;
  else if (act == "createBack") pending_ = ScreenAction::Back;
  else if (act == "createStart") pending_ = ScreenAction::Start;
  else if (act == "potion") hudActs_.push_back(HudAction::Potion);
  else if (act == "mount") hudActs_.push_back(HudAction::Mount);
  else if (act == "camNorth") hudActs_.push_back(HudAction::CamNorth);
  else if (act == "miniSize") hudActs_.push_back(HudAction::MiniSize);
  else if (act == "captionClose") hudActs_.push_back(HudAction::CaptionClose);
  else if (form_ && act == "pickModel") form_->model = std::min(index(), kCharacterModels.size() - 1), cardsDirty_ = true;
  else if (form_ && act == "pickOrigin") form_->origin = std::min(index(), data_.origins.size() - 1), cardsDirty_ = true;
  else if (form_ && act == "pickStart") form_->start = std::min(index(), data_.starts.size() - 1), cardsDirty_ = true;
}

GameUi::ScreenAction GameUi::screenInput(const InputState& in, CreationForm& form) {
  ScreenAction r = pending_;
  pending_ = ScreenAction::None;
  const bool creating = form_ != nullptr;
  if (creating) {
    if (auto* name = dynamic_cast<Rml::ElementFormControlInput*>(byId("c-name"))) form.name = name->GetValue();
    if (cardsDirty_) {
      renderCards(form);
      cardsDirty_ = false;
    }
    if (in.hit(Key::Enter)) r = ScreenAction::Start;
    if (in.hit(Key::Escape)) r = ScreenAction::Back;
  } else if (in.hit(Key::Enter)) {
    r = ScreenAction::NewGame;
  }
  return r;
}

void GameUi::notice(std::string_view text) {
  Rml::Element* n = byId("notice");
  if (!n) return;
  UiSystem::setHidden(n, text.empty());
  if (!text.empty()) UiSystem::setText(n, text);
}

}  // namespace rpg::client
