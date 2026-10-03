#pragma once
// Câmera em terceira pessoa (game/player.js `updateCamera`): órbita com zoom suave, mira sobre o
// ombro (V alterna o lado), spring arm contra relevo e colisores, recuo e tremor, volta ao norte,
// modo de observação do céu e câmera livre de depuração.
#include <optional>
#include <random>

#include <glm/glm.hpp>

#include "client/Input.h"
#include "client/game/ViewCamera.h"

namespace rpg {
class StaticMap;
}

namespace rpg::client {

// O que a câmera precisa saber do personagem que ela segue.
struct CameraSubject {
  glm::dvec3 pos{0.0};
  double yaw = 0;
  double airY = 0;        // altura do pulo acima do chão (0 no chão)
  bool mounted = false;
  double crouchW = 0;
  double speedNow = 0;
  bool aiming = false;
};

enum class CamSensitivity : std::uint8_t { Baixa, Media, Alta };

struct CameraPrefs {
  CamSensitivity sens = CamSensitivity::Media;
  bool invert = false;
  double sensFactor() const { return sens == CamSensitivity::Baixa ? 0.7 : sens == CamSensitivity::Alta ? 1.5 : 1.0; }
};

class ThirdPersonCamera {
 public:
  static constexpr double kPitchDefault = 0.67, kPitchMin = -0.45, kPitchMax = 1.35;
  static constexpr double kOrbitMin = 0.04;
  static constexpr double kAimDist = 4.8, kAimPitchMin = -0.55, kAimPitchMax = 0.95;
  static constexpr double kAimFov = 35, kAimSide = 0.88, kFovBase = 55;
  static constexpr double kWalk = 3, kRun = 7.2;

  // `uiOpen`: painel aberto (a câmera não gira nem aproxima). `map`: relevo e colisores do mapa atual.
  void update(double dt, bool snap, const CameraSubject& p, const InputState& in, bool uiOpen, const StaticMap& map);

  const ViewCamera& view() const { return view_; }
  ViewCamera& view() { return view_; }

  // yaw da câmera: WASD é relativo a ele (PlayerCommand::camYaw)
  double yaw() const { return yaw_; }
  double pitch() const { return pitch_; }
  double aimT() const { return aimT_; }
  void setYaw(double y) { yaw_ = y; }

  void addRecoil(double pitchKick, double yawKick) { recoilPitch_ -= pitchKick; recoilYaw_ += yawKick; }
  void addShake(double amount) { shake_ = std::min(1.2, shake_ + amount); }
  // Tab + 1/2/3
  void setPreset(int n);
  void toggleShoulder() { shoulder_ = shoulder_ == -1 ? 1 : -1; }
  int shoulder() const { return shoulder_; }
  void resetNorth() { resetting_ = true; }

  void setDragging(bool d) { dragging_ = d; }
  bool dragging() const { return dragging_; }

  // Observação do céu (stars.js): câmera sobe e olha na direção dada; vazio volta a seguir.
  void setSkyDirection(std::optional<glm::dvec3> dir) { skyDir_ = dir; }
  // Câmera livre de depuração (__demo.freecam).
  void setFreeCam(std::optional<std::pair<glm::dvec3, glm::dvec3>> posLook) { freeCam_ = posLook; }

  // Posiciona um enquadramento fixo (tela de título, capturas).
  void setFixed(const glm::dvec3& pos, const glm::dvec3& look, double fovDeg);

  CameraPrefs prefs;

 private:
  ViewCamera view_;
  double yaw_ = 0, pitch_ = kPitchDefault, dist_ = 14, targetDist_ = 14, actualDist_ = 0;
  double aimT_ = 0, recoilPitch_ = 0, recoilYaw_ = 0, shake_ = 0;
  int shoulder_ = 1;
  double curShoulder_ = 1;
  std::optional<double> tgtY_;
  double lead_ = 0;
  bool resetting_ = false, dragging_ = false, hasPose_ = false;
  glm::dvec3 look_{0.0};
  std::optional<glm::dvec3> skyDir_;
  std::optional<std::pair<glm::dvec3, glm::dvec3>> freeCam_;
  std::mt19937 shakeRng_{7};  // tremor: aleatoriedade só visual
};

}  // namespace rpg::client
