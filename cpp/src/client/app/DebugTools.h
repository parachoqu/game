#pragma once
// Ferramentas de depuração em Dear ImGui (backends SDL3 + SDL_GPU), só com --dev e janela: F3 abre.
// Mostram o quadro (tempo, desenhos), o jogador e o mundo, e mandam os comandos de desenvolvimento
// (window.__demo da demo) como Requests — o servidor só os aceita em modo de desenvolvimento.
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL_gpu.h>

#include "core/protocol/Requests.h"

struct SDL_Window;
union SDL_Event;

namespace rpg::client {

class DebugTools {
 public:
  DebugTools(SDL_Window* window, SDL_GPUDevice* device, SDL_GPUTextureFormat target);
  ~DebugTools();
  DebugTools(const DebugTools&) = delete;
  DebugTools& operator=(const DebugTools&) = delete;

  void event(const SDL_Event& e);
  void toggle() { open_ = !open_; }
  bool open() const { return open_; }
  // O mouse ou o teclado estão sobre a janela das ferramentas (o jogo não usa o evento).
  bool wantsMouse() const;
  bool wantsKeyboard() const;

  struct Info {
    double frameMs = 0, fps = 0;
    int drawCalls = 0, instances = 0;
    double x = 0, y = 0, z = 0, yaw = 0;
    std::string map, state, zone, place, clock;
    double hp = 0, maxHp = 0, vigor = 0, coins = 0;
    int entities = 0;
    bool freeCam = false;
  };
  struct Output {
    std::vector<protocol::Request> requests;
    bool toggleFreeCam = false;
    std::string view;  // enquadramento escolhido (02_mercado…)
  };
  // Monta a janela deste quadro (chame antes do render).
  Output frame(const Info& info, const std::vector<std::string>& views);
  // Desenha na textura de saída (Renderer::Frame::overlay).
  void render(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target);

 private:
  bool open_ = false, built_ = false;
  float coins_ = 100, hurt_ = 20;
};

}  // namespace rpg::client
