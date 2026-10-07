#include "client/ui/UiSystem.h"

#include <functional>
#include <stdexcept>

#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementText.h>

#include "client/ui/UiDecorators.h"
#include "core/Log.h"
#include "core/Paths.h"

namespace rpg::client {

// Relógio e registro do RmlUi.
class UiSystem::System final : public Rml::SystemInterface {
 public:
  double time = 0;
  double GetElapsedTime() override { return time; }
  // texturas dinâmicas (dyn:…) não são arquivos: o nome passa como está
  void JoinPath(Rml::String& out, const Rml::String& document, const Rml::String& path) override {
    if (path.rfind("dyn:", 0) == 0) out = path;
    else Rml::SystemInterface::JoinPath(out, document, path);
  }
  bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
    if (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT) log::error("ui: {}", message);
    else if (type == Rml::Log::LT_WARNING) log::warn("ui: {}", message);
    else log::debug("ui: {}", message);
    return true;
  }
};

// Cliques (data-act) de todos os documentos.
class UiSystem::Listener final : public Rml::EventListener {
 public:
  explicit Listener(UiSystem& ui) : ui_(ui) {}
  void ProcessEvent(Rml::Event& ev) override { ui_.dispatch(ev); }

 private:
  UiSystem& ui_;
};

namespace {

// Uma instância por processo: o RmlUi é global.
bool g_alive = false;

// text-transform: uppercase do RmlUi só sobe o ASCII; aqui sobem também as letras acentuadas
// (U+00E0–U+00FE, sem o ÷), como o navegador faz. Devolve se mudou algo.
bool upperLatin1(std::string& s) {
  bool changed = false;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c >= 'a' && c <= 'z') {
      s[i] = static_cast<char>(c - 32);
      changed = true;
    } else if (c == 0xC3 && i + 1 < s.size()) {
      const auto d = static_cast<unsigned char>(s[i + 1]);
      if (d >= 0xA0 && d <= 0xBE && d != 0xB7) {
        s[i + 1] = static_cast<char>(d - 0x20);
        changed = true;
      }
      ++i;
    }
  }
  return changed;
}

// O atributo hidden vale mais que qualquer regra (no navegador, display:none !important; o RCSS do
// RmlUi não tem !important): vira propriedade local.
void applyHidden(Rml::Element* e) {
  if (!e) return;
  if (e->HasAttribute("hidden")) e->SetProperty(Rml::PropertyId::Display, Rml::Property(Rml::Style::Display::None));
  for (int i = 0; i < e->GetNumChildren(); ++i) applyHidden(e->GetChild(i));
}

// Depois do estilo: (1) texto solto dentro de flex vira <span> — o RmlUi só dispõe elementos num
// contêiner flex e descartaria o texto, que no navegador vira item anônimo; (2) o uppercase acima.
void fixPass(Rml::Element* e, bool& changed) {
  if (!e || !e->IsVisible()) return;
  const Rml::Style::Display d = e->GetComputedValues().display();
  if ((d == Rml::Style::Display::Flex || d == Rml::Style::Display::InlineFlex) && e->GetOwnerDocument()) {
    for (int i = 0; i < e->GetNumChildren(); ++i) {
      Rml::Element* c = e->GetChild(i);
      auto* t = dynamic_cast<Rml::ElementText*>(c);
      if (!t || t->GetText().find_first_not_of(" \t\n\r") == Rml::String::npos) continue;
      Rml::ElementPtr span = e->GetOwnerDocument()->CreateElement("t");  // etiqueta sem regra própria: herda o texto do pai
      Rml::Element* sp = e->InsertBefore(std::move(span), c);
      sp->AppendChild(e->RemoveChild(c));
      changed = true;
    }
  }
  if (auto* t = dynamic_cast<Rml::ElementText*>(e)) {
    if (e->GetComputedValues().text_transform() == Rml::Style::TextTransform::Uppercase) {
      Rml::String text = t->GetText();
      bool lower = false;
      for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == 0xC3 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) >= 0xA0) lower = true;
      }
      if (lower && upperLatin1(text)) {
        t->SetText(text);
        changed = true;
      }
    }
    return;
  }
  for (int i = 0; i < e->GetNumChildren(); ++i) fixPass(e->GetChild(i), changed);
}

Rml::Input::KeyIdentifier keyOf(Key k) {
  using K = Rml::Input::KeyIdentifier;
  switch (k) {
    case Key::W: return K::KI_W;
    case Key::A: return K::KI_A;
    case Key::S: return K::KI_S;
    case Key::D: return K::KI_D;
    case Key::Q: return K::KI_Q;
    case Key::E: return K::KI_E;
    case Key::R: return K::KI_R;
    case Key::F: return K::KI_F;
    case Key::C: return K::KI_C;
    case Key::V: return K::KI_V;
    case Key::M: return K::KI_M;
    case Key::I: return K::KI_I;
    case Key::B: return K::KI_B;
    case Key::J: return K::KI_J;
    case Key::N: return K::KI_N;
    case Key::Digit1: return K::KI_1;
    case Key::Digit2: return K::KI_2;
    case Key::Digit3: return K::KI_3;
    case Key::Space: return K::KI_SPACE;
    case Key::Tab: return K::KI_TAB;
    case Key::Escape: return K::KI_ESCAPE;
    case Key::Enter: return K::KI_RETURN;
    case Key::Backspace: return K::KI_BACK;
    case Key::LShift: return K::KI_LSHIFT;
    case Key::RShift: return K::KI_RSHIFT;
    case Key::LCtrl: return K::KI_LCONTROL;
    case Key::RCtrl: return K::KI_RCONTROL;
    case Key::Left: return K::KI_LEFT;
    case Key::Right: return K::KI_RIGHT;
    case Key::Up: return K::KI_UP;
    case Key::Down: return K::KI_DOWN;
    case Key::F1: return K::KI_F1;
    case Key::F2: return K::KI_F2;
    case Key::F3: return K::KI_F3;
    case Key::F12: return K::KI_F12;
    default: return K::KI_UNKNOWN;
  }
}

}  // namespace

UiSystem::UiSystem(const Options& o) : system_(std::make_unique<System>()), render_(std::make_unique<RmlRender>()), uiDir_(o.uiDir) {
  if (g_alive) throw std::runtime_error("UiSystem: o RmlUi só aceita uma instância por processo");
  listener_ = std::make_unique<Listener>(*this);
  Rml::SetSystemInterface(system_.get());
  Rml::SetRenderInterface(render_.get());
  if (!Rml::Initialise()) throw std::runtime_error("Rml::Initialise falhou");
  g_alive = true;
  decorators_ = registerUiDecorators();
  render_->setDynamicTexture("dyn:grain", 120, 120, stoneGrain(120));
  // fontes: a de títulos da demo (Fraunces) e as DejaVu no lugar das fontes do sistema
  for (const char* f : {"Fraunces-SemiBold.ttf", "DejaVuSans.ttf", "DejaVuSans-Bold.ttf", "DejaVuSerif.ttf", "DejaVuSerif-Italic.ttf",
                        "DejaVuSerif-Bold.ttf", "DejaVuSansMono.ttf", "DejaVuSansMono-Bold.ttf"}) {
    const std::filesystem::path p = o.fontsDir / f;
    if (std::filesystem::exists(p)) Rml::LoadFontFace(pathToUtf8(p), std::string_view(f) == "DejaVuSans.ttf");
  }
  context_ = Rml::CreateContext("main", Rml::Vector2i(std::max(1, o.width), std::max(1, o.height)));
  if (!context_) throw std::runtime_error("Rml::CreateContext falhou");
}

UiSystem::~UiSystem() {
  if (context_) Rml::RemoveContext(context_->GetName());
  Rml::Shutdown();
  g_alive = false;
}

Rml::ElementDocument* UiSystem::load(const std::string& file) {
  const std::filesystem::path p = uiDir_ / pathFromUtf8(file);
  Rml::ElementDocument* doc = context_->LoadDocument(pathToUtf8(p));
  if (!doc) throw std::runtime_error("documento da interface não carregou: " + pathToUtf8(p));
  applyHidden(doc);
  textDirty_ = true;
  doc->AddEventListener(Rml::EventId::Click, listener_.get());
  doc->AddEventListener(Rml::EventId::Submit, listener_.get());
  return doc;
}

void UiSystem::resize(int width, int height) { context_->SetDimensions(Rml::Vector2i(std::max(1, width), std::max(1, height))); }

void UiSystem::input(const InputState& in) {
  mods_ = 0;
  if (in.down(Key::LCtrl) || in.down(Key::RCtrl)) mods_ |= Rml::Input::KM_CTRL;
  if (in.down(Key::LShift) || in.down(Key::RShift)) mods_ |= Rml::Input::KM_SHIFT;
  if (!in.locked) context_->ProcessMouseMove(static_cast<int>(in.mouseX), static_cast<int>(in.mouseY), mods_);
  if (in.leftPressed) context_->ProcessMouseButtonDown(0, mods_);
  if (in.leftReleased) context_->ProcessMouseButtonUp(0, mods_);
  if (in.right && !prevRight_) context_->ProcessMouseButtonDown(1, mods_);
  if (!in.right && prevRight_) context_->ProcessMouseButtonUp(1, mods_);
  prevRight_ = in.right;
  if (in.wheel) context_->ProcessMouseWheel(static_cast<float>(in.wheel), mods_);
  for (std::size_t k = 0; k < InputState::kKeys; ++k) {
    const auto key = keyOf(static_cast<Key>(k));
    if (key == Rml::Input::KI_UNKNOWN) continue;
    if (in.pressed.test(k)) context_->ProcessKeyDown(key, mods_);
    if (in.released.test(k)) context_->ProcessKeyUp(key, mods_);
  }
  if (!in.text.empty()) context_->ProcessTextInput(in.text);
}

bool UiSystem::wantsMouse() const {
  const Rml::Element* h = context_->GetHoverElement();
  return h && h != h->GetOwnerDocument();
}

bool UiSystem::wantsKeyboard() const {
  const Rml::Element* f = context_->GetFocusElement();
  if (!f) return false;
  const Rml::String tag = f->GetTagName();
  return tag == "input" || tag == "textarea";
}

void UiSystem::update(double time) {
  time_ = time;
  system_->time = time;
  context_->Update();
  if (textDirty_) {
    textDirty_ = false;
    for (int pass = 0; pass < 3; ++pass) {
      bool changed = false;
      for (int i = 0; i < context_->GetNumDocuments(); ++i) fixPass(context_->GetDocument(i), changed);
      if (!changed) break;
      context_->Update();  // o texto embrulhado ganha estilo; a segunda volta sobe as maiúsculas
    }
  }
}

void UiSystem::draw() {
  const Rml::Vector2i d = context_->GetDimensions();
  render_->beginFrame(d.x, d.y);
  context_->Render();
}

// ---------------------------------------------------------------- DOM

std::string UiSystem::escape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

std::string UiSystem::lower(std::string s) {
  for (std::size_t i = 0; i < s.size(); ++i) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c >= 'A' && c <= 'Z') {
      s[i] = static_cast<char>(c + 32);
    } else if (c == 0xC3 && i + 1 < s.size()) {
      const auto d = static_cast<unsigned char>(s[i + 1]);
      if (d >= 0x80 && d <= 0x9E && d != 0x97) s[i + 1] = static_cast<char>(d + 0x20);
      ++i;
    }
  }
  return s;
}

void UiSystem::setText(Rml::Element* e, std::string_view text) {
  if (!e) return;
  setInner(e, escape(text));
}

void UiSystem::setInner(Rml::Element* e, const std::string& rml) {
  if (!e) return;
  // compara com o que foi posto da última vez (o DOM pode ter sido mexido pelo fixPass)
  const auto h = static_cast<int>(std::hash<std::string>{}(rml) & 0x7fffffff);
  if (e->HasAttribute("data-rml") && e->GetAttribute<int>("data-rml", -1) == h) return;
  e->SetAttribute("data-rml", h);
  e->SetInnerRML(rml);
  applyHidden(e);
  textDirty_ = true;
}

void UiSystem::setHidden(Rml::Element* e, bool hidden) {
  if (!e) return;
  if (hidden && !e->HasAttribute("hidden")) {
    e->SetAttribute("hidden", "");
    e->SetProperty(Rml::PropertyId::Display, Rml::Property(Rml::Style::Display::None));
  } else if (!hidden && e->HasAttribute("hidden")) {
    e->RemoveAttribute("hidden");
    e->RemoveProperty(Rml::PropertyId::Display);
    textDirty_ = true;
  }
}

void UiSystem::setClass(Rml::Element* e, const std::string& name, bool on) {
  if (e && e->IsClassSet(name) != on) {
    e->SetClass(name, on);
    textDirty_ = true;
  }
}

void UiSystem::setStyle(Rml::Element* e, const std::string& property, const std::string& value) {
  if (!e) return;
  const Rml::Property* cur = e->GetLocalProperty(property);
  if (cur && cur->ToString() == value) return;
  e->SetProperty(property, value);
}

void UiSystem::dispatch(Rml::Event& ev) {
  if (!action_) return;
  for (Rml::Element* e = ev.GetTargetElement(); e; e = e->GetParentNode()) {
    if (!e->HasAttribute("data-act")) continue;
    if (e->HasAttribute("disabled")) return;
    const std::string act = e->GetAttribute<Rml::String>("data-act", "");
    const std::string arg = e->GetAttribute<Rml::String>("data-arg", "");
    action_(act, arg, e);
    return;
  }
}

}  // namespace rpg::client
