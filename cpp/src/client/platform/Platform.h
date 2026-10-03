#pragma once
// Plataforma SDL3: janela, eventos e relógio. Converte eventos em InputState (engine/input.js):
// teclas por scancode, mouse com arrasto, rodinha e modo relativo (mira com ponteiro preso).
#include <cstdint>
#include <functional>
#include <string>

#include "client/Input.h"

struct SDL_Window;
union SDL_Event;

namespace rpg::client {

struct PlatformOptions {
  int width = 1280, height = 720;
  bool fullscreen = false;
  bool headless = false;  // sem janela nem vídeo (capturas e testes): só a GPU
  std::string title = "Projeto Game";
};

class Platform {
 public:
  explicit Platform(const PlatformOptions& o);
  ~Platform();
  Platform(const Platform&) = delete;
  Platform& operator=(const Platform&) = delete;

  SDL_Window* window() const { return window_; }
  bool headless() const { return window_ == nullptr; }

  // Lê os eventos pendentes para `in` (chame `in.endFrame()` depois de usar o quadro).
  void pump(InputState& in);
  void setRelativeMouse(bool on);
  void setTextInput(bool on);
  void setTitle(const std::string& t);
  // Cada evento SDL também vai para `hook` antes de virar InputState (ferramentas de depuração).
  void setEventHook(std::function<void(const SDL_Event&)> hook) { hook_ = std::move(hook); }

  double now() const;  // s desde o início
  void sleepMs(int ms) const;

 private:
  SDL_Window* window_ = nullptr;
  bool relative_ = false, textInput_ = false;
  int w_ = 1280, h_ = 720;
  std::function<void(const SDL_Event&)> hook_;
};

}  // namespace rpg::client
