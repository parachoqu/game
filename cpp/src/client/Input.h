#pragma once
// Estado de input de um quadro, independente da plataforma (engine/input.js). A plataforma (SDL3)
// preenche; o resto do cliente só lê. Teclas por posição física (scancode): WASD funciona igual em
// ABNT, US ou AZERTY, como o `e.code` da demo.
#include <bitset>
#include <cstdint>
#include <string>

namespace rpg::client {

enum class Key : std::uint8_t {
  W, A, S, D, Q, E, R, F, C, V, M, I, B, J, N, Digit1, Digit2, Digit3,
  Space, Tab, Escape, Enter, Backspace, LShift, RShift, LCtrl, RCtrl,
  Left, Right, Up, Down, F1, F2, F3, F12,
  Count
};

struct InputState {
  static constexpr std::size_t kKeys = static_cast<std::size_t>(Key::Count);
  std::bitset<kKeys> keys, pressed, released;

  // janela (pixels lógicos)
  int width = 1280, height = 720;
  bool focused = true;

  // mouse: posição do cursor; com a mira presa ao centro, o movimento vira `lookDX/DY`
  double mouseX = 640, mouseY = 360;
  double rawX = 640, rawY = 360;         // onde o cursor de verdade está
  double lookDX = 0, lookDY = 0;         // movimento relativo com o ponteiro preso (mira)
  double dragDX = 0, dragDY = 0;         // arrasto com o botão do meio
  double leftDX = 0, leftDY = 0;         // arrasto com o botão esquerdo
  bool left = false, right = false, middle = false;
  bool leftPressed = false, rightPressed = false, leftReleased = false;
  int wheel = 0;                         // +1 por clique para baixo (afasta a câmera)
  bool locked = false;                   // ponteiro preso (modo relativo)

  std::string text;                      // texto digitado neste quadro (UTF-8)
  bool quit = false;

  bool down(Key k) const { return keys.test(static_cast<std::size_t>(k)); }
  bool hit(Key k) const { return pressed.test(static_cast<std::size_t>(k)); }
  bool up(Key k) const { return released.test(static_cast<std::size_t>(k)); }

  // Fim do quadro (input.js `endFrame`): zera toques, rodinha e deslocamentos.
  void endFrame() {
    pressed.reset();
    released.reset();
    leftPressed = rightPressed = leftReleased = false;
    wheel = 0;
    lookDX = lookDY = dragDX = dragDY = leftDX = leftDY = 0;
    text.clear();
  }
};

}  // namespace rpg::client
