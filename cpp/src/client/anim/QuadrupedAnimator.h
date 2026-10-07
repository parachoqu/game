#pragma once
// Quadrúpedes da demo (characters.js): a leoa (`quadruped` + `animateBeast`), o cão do vazio (`hound` +
// `animateHound`, malha rígida que paira) e o cavalo (`makeMount` + `animateMount`). Leoa e cavalo
// misturam o ocioso com a caminhada pela velocidade; por cima, o corpo inclina na curva e no declive.
#include <cstdint>
#include <functional>
#include <vector>

#include "client/anim/Mixer.h"
#include "core/Rng.h"

namespace rpg::client::anim {

// O `s` de animateBeast/animateMount.
struct QuadrupedInput {
  double dt = 0.016;
  double mps = 0;   // m/s (a montaria converte speed·12,5; a fera speed·8 quando não há mps)
  bool down = false, stun = false;
  double windup = -1, attack = -1;  // progresso (-1 = nenhum)
};

class QuadrupedAnimator {
 public:
  enum class Kind : std::uint8_t { Beast, Hound, Mount };
  QuadrupedAnimator(const Rig& rig, Kind kind, double scale, std::uint32_t seed, std::function<double()> random = {});

  bool animate(const QuadrupedInput& s, const AnimWorld& w);

  Kind kind() const { return kind_; }
  const Pose& pose() const { return pose_; }
  glm::mat4 bodyMatrix() const;

 private:
  void step(double dt, double mps, double base, double maxTs, bool frozen);
  void posture(double dt, double w, const AnimWorld& world);
  void animateHound(const QuadrupedInput& s, double dt);

  const Rig& rig_;
  Kind kind_;
  Mixer mixer_;
  Pose pose_;
  int idle_ = -1, walk_ = -1;
  std::vector<int> zeroed_;  // ossos de BENT cuja posição a demo zera (ver o construtor)
  Pcg32 rng_;
  std::function<double()> random_;
  double rand() { return random_ ? random_() : rng_.next(); }
  double scale_ = 1;
  AnimLod lod_;
  glm::vec3 bodyRot_{0.0f}, bodyPos_{0.0f}, bodyScale_{1.0f};
  double walkW_ = 0, phase_ = 0, seed_ = 0, downT_ = 0;
  double lean_ = 0, tiltX_ = 0, tiltZ_ = 0, prevYaw_ = 0;
};

}  // namespace rpg::client::anim
