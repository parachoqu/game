#pragma once
// Preferências do jogador (o localStorage da demo: câmera, qualidade) e o último personagem usado, para
// o "Continuar trajetória" do título. Um JSON pequeno no diretório do usuário; sem arquivo (capturas,
// testes) tudo fica só na memória.
#include <filesystem>
#include <optional>
#include <string>

#include "client/game/ThirdPersonCamera.h"

namespace rpg::client {

struct Settings {
  struct Profile {
    std::string name, origin, start, model;
  };

  CameraPrefs camera;
  std::string quality = "alta";
  bool qualityChosen = false;  // quality.js QUALITY.chosen: escolhida pelo jogador (sem ajuste automático)
  bool muted = false;
  std::optional<Profile> last;  // personagem da última entrada no mundo

  // Ausente ou inválido: os padrões (o erro vai para o log).
  static Settings load(const std::filesystem::path& file);
  bool save(const std::filesystem::path& file) const;
};

}  // namespace rpg::client
