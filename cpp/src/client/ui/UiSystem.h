#pragma once
// A interface em RmlUi (o index.html + styles.css da demo portados para RML/RCSS): inicialização do
// RmlUi com as fontes do cliente, um contexto do tamanho da saída, documentos, input vindo do
// InputState e ajudas para mexer no DOM como ui/*.js mexe (texto, classe, atributo, visível,
// innerHTML, cliques por data-act). Sem SDL: o desenho fica gravado em RmlRender.
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "client/Input.h"
#include "client/ui/RmlRender.h"

namespace Rml {
class Context;
class DecoratorInstancer;
class Element;
class ElementDocument;
class Event;
}  // namespace Rml

namespace rpg::client {

class UiSystem {
 public:
  struct Options {
    std::filesystem::path uiDir;     // assets/ui (documentos e folhas de estilo)
    std::filesystem::path fontsDir;  // assets/fonts
    int width = 1280, height = 720;
  };
  explicit UiSystem(const Options& o);
  ~UiSystem();
  UiSystem(const UiSystem&) = delete;
  UiSystem& operator=(const UiSystem&) = delete;

  RmlRender& render() { return *render_; }
  Rml::Context* context() { return context_; }

  // Carrega um documento de uiDir (sem mostrar). Lança std::runtime_error se não existir.
  Rml::ElementDocument* load(const std::string& file);

  void resize(int width, int height);
  // Repassa o quadro de input ao RmlUi (mouse, rodinha, teclas, texto).
  void input(const InputState& in);
  // O mouse está sobre algo da interface que recebe cliques (o clique não vai para o mundo).
  bool wantsMouse() const;
  // Um campo de texto tem o foco (as teclas não vão para o jogo).
  bool wantsKeyboard() const;

  // Avança animações e layout (`time` em segundos, relógio da aplicação) e grava o desenho.
  void update(double time);
  void draw();

  // ---- DOM, como em ui/*.js
  static void setText(Rml::Element* e, std::string_view text);       // textContent (escapa)
  static void setInner(Rml::Element* e, const std::string& rml);     // innerHTML
  static void setHidden(Rml::Element* e, bool hidden);               // atributo hidden
  static void setClass(Rml::Element* e, const std::string& name, bool on);
  static void setStyle(Rml::Element* e, const std::string& property, const std::string& value);
  static std::string escape(std::string_view text);
  static std::string lower(std::string text);  // toLowerCase (ASCII e Latin-1)

  // Cliques em elementos com data-act (e data-arg), de qualquer documento: (ação, argumento, elemento).
  using ActionHandler = std::function<void(const std::string& act, const std::string& arg, Rml::Element* el)>;
  void onAction(ActionHandler h) { action_ = std::move(h); }

 private:
  class System;
  class Listener;
  void dispatch(Rml::Event& ev);

  std::unique_ptr<System> system_;
  std::unique_ptr<RmlRender> render_;
  std::unique_ptr<Listener> listener_;
  std::vector<std::unique_ptr<Rml::DecoratorInstancer>> decorators_;
  Rml::Context* context_ = nullptr;
  std::filesystem::path uiDir_;
  ActionHandler action_;
  double time_ = 0;
  static inline bool textDirty_ = true;  // texto novo: passar o text-transform das letras acentuadas
  int mods_ = 0;
  bool prevRight_ = false;
};

}  // namespace rpg::client
