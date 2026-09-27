#include "client/platform/Platform.h"

#include <optional>
#include <stdexcept>

#include <SDL3/SDL.h>

namespace rpg::client {

namespace {

std::optional<Key> mapKey(SDL_Scancode s) {
  switch (s) {
    case SDL_SCANCODE_W: return Key::W;
    case SDL_SCANCODE_A: return Key::A;
    case SDL_SCANCODE_S: return Key::S;
    case SDL_SCANCODE_D: return Key::D;
    case SDL_SCANCODE_Q: return Key::Q;
    case SDL_SCANCODE_E: return Key::E;
    case SDL_SCANCODE_R: return Key::R;
    case SDL_SCANCODE_F: return Key::F;
    case SDL_SCANCODE_C: return Key::C;
    case SDL_SCANCODE_V: return Key::V;
    case SDL_SCANCODE_M: return Key::M;
    case SDL_SCANCODE_I: return Key::I;
    case SDL_SCANCODE_B: return Key::B;
    case SDL_SCANCODE_J: return Key::J;
    case SDL_SCANCODE_N: return Key::N;
    case SDL_SCANCODE_1:
    case SDL_SCANCODE_KP_1: return Key::Digit1;
    case SDL_SCANCODE_2:
    case SDL_SCANCODE_KP_2: return Key::Digit2;
    case SDL_SCANCODE_3:
    case SDL_SCANCODE_KP_3: return Key::Digit3;
    case SDL_SCANCODE_SPACE: return Key::Space;
    case SDL_SCANCODE_TAB: return Key::Tab;
    case SDL_SCANCODE_ESCAPE: return Key::Escape;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: return Key::Enter;
    case SDL_SCANCODE_BACKSPACE: return Key::Backspace;
    case SDL_SCANCODE_LSHIFT: return Key::LShift;
    case SDL_SCANCODE_RSHIFT: return Key::RShift;
    case SDL_SCANCODE_LCTRL: return Key::LCtrl;
    case SDL_SCANCODE_RCTRL: return Key::RCtrl;
    case SDL_SCANCODE_LEFT: return Key::Left;
    case SDL_SCANCODE_RIGHT: return Key::Right;
    case SDL_SCANCODE_UP: return Key::Up;
    case SDL_SCANCODE_DOWN: return Key::Down;
    case SDL_SCANCODE_F1: return Key::F1;
    case SDL_SCANCODE_F2: return Key::F2;
    case SDL_SCANCODE_F3: return Key::F3;
    case SDL_SCANCODE_F12: return Key::F12;
    default: return std::nullopt;
  }
}

}  // namespace

Platform::Platform(const PlatformOptions& o) : w_(o.width), h_(o.height) {
  if (o.headless) {
    // sem janela: o SDL_GPU ainda precisa do vídeo para carregar o Vulkan; o driver "offscreen"
    // não precisa de servidor gráfico (CI, capturas automáticas)
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());
    return;
  }
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());
  SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  if (o.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
  window_ = SDL_CreateWindow(o.title.c_str(), o.width, o.height, flags);
  if (!window_) throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());
}

Platform::~Platform() {
  if (window_) SDL_DestroyWindow(window_);
  SDL_Quit();
}

void Platform::setTitle(const std::string& t) {
  if (window_) SDL_SetWindowTitle(window_, t.c_str());
}

void Platform::setRelativeMouse(bool on) {
  if (!window_ || on == relative_) return;
  relative_ = on;
  SDL_SetWindowRelativeMouseMode(window_, on);
}

void Platform::setTextInput(bool on) {
  if (!window_ || on == textInput_) return;
  textInput_ = on;
  if (on) SDL_StartTextInput(window_);
  else SDL_StopTextInput(window_);
}

double Platform::now() const { return static_cast<double>(SDL_GetTicksNS()) * 1e-9; }
void Platform::sleepMs(int ms) const { SDL_Delay(static_cast<Uint32>(ms)); }

void Platform::pump(InputState& in) {
  in.locked = relative_;
  if (!window_) {
    in.width = w_;
    in.height = h_;
    return;
  }
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    switch (e.type) {
      case SDL_EVENT_QUIT:
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED: in.quit = true; break;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        in.focused = false;
        in.keys.reset();
        in.left = in.right = in.middle = false;
        setRelativeMouse(false);
        break;
      case SDL_EVENT_WINDOW_FOCUS_GAINED: in.focused = true; break;
      case SDL_EVENT_KEY_DOWN:
        if (const auto k = mapKey(e.key.scancode)) {
          const auto i = static_cast<std::size_t>(*k);
          if (!in.keys.test(i)) in.pressed.set(i);
          in.keys.set(i);
        }
        break;
      case SDL_EVENT_KEY_UP:
        if (const auto k = mapKey(e.key.scancode)) {
          const auto i = static_cast<std::size_t>(*k);
          if (in.keys.test(i)) in.released.set(i);
          in.keys.reset(i);
        }
        break;
      case SDL_EVENT_TEXT_INPUT: in.text += e.text.text; break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button == SDL_BUTTON_LEFT) {
          in.left = true;
          in.leftPressed = true;
        } else if (e.button.button == SDL_BUTTON_RIGHT) {
          in.right = true;
          in.rightPressed = true;
        } else if (e.button.button == SDL_BUTTON_MIDDLE) {
          in.middle = true;
        }
        break;
      case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button == SDL_BUTTON_LEFT) {
          in.left = false;
          in.leftReleased = true;
        } else if (e.button.button == SDL_BUTTON_RIGHT) {
          in.right = false;
        } else if (e.button.button == SDL_BUTTON_MIDDLE) {
          in.middle = false;
        }
        break;
      case SDL_EVENT_MOUSE_MOTION:
        in.rawX = static_cast<double>(e.motion.x);
        in.rawY = static_cast<double>(e.motion.y);
        if (relative_) {
          in.lookDX += static_cast<double>(e.motion.xrel);
          in.lookDY += static_cast<double>(e.motion.yrel);
          break;
        }
        in.mouseX = static_cast<double>(e.motion.x);
        in.mouseY = static_cast<double>(e.motion.y);
        if (in.middle) {
          in.dragDX += static_cast<double>(e.motion.xrel);
          in.dragDY += static_cast<double>(e.motion.yrel);
        }
        if (in.left) {
          in.leftDX += static_cast<double>(e.motion.xrel);
          in.leftDY += static_cast<double>(e.motion.yrel);
        }
        break;
      case SDL_EVENT_MOUSE_WHEEL:
        // input.js: Math.sign(deltaY), positivo = afasta; no SDL rolar para baixo é y < 0
        if (e.wheel.y < 0) in.wheel += 1;
        else if (e.wheel.y > 0) in.wheel -= 1;
        break;
      default: break;
    }
  }
  int w = 0, h = 0;
  SDL_GetWindowSize(window_, &w, &h);
  in.width = w;
  in.height = h;
  // mirando, a mira fica no centro da tela
  if (relative_) {
    in.mouseX = w * 0.5;
    in.mouseY = h * 0.5;
  }
}

}  // namespace rpg::client
