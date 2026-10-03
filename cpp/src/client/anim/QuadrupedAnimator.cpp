#include "client/anim/QuadrupedAnimator.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace rpg::client::anim {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double HOVER = 0.3;  // altura do cão acima do chão, em metros (escala 1)

double clamp(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
double approach(double v, double t, double k) { return v + (t - v) * std::min(1.0, k); }

}  // namespace

QuadrupedAnimator::QuadrupedAnimator(const Rig& rig, Kind kind, double scale, std::uint32_t seed, std::function<double()> random)
    : rig_(rig), kind_(kind), mixer_(rig), pose_(rig.rest), rng_(seed), random_(std::move(random)), scale_(scale) {
  const auto s = static_cast<float>(scale);
  bodyScale_ = glm::vec3(s);
  if (kind == Kind::Hound) {
    phase_ = rand() * 6;
    seed_ = rand() * 6;
    return;
  }
  // quadruped(): ocioso e caminhada tocando, a caminhada com peso zero
  if (const Clip* c = rig.clip("idle")) idle_ = mixer_.add(c);
  if (const Clip* c = rig.clip("walk")) walk_ = mixer_.add(c);
  // quadruped() guarda só a rotação da pose base (basePose): no primeiro mixerStep a posição dos ossos
  // de BENT volta a (0, 0, 0) e fica assim, já que nenhum clipe a anima. Na leoa isso encolhe a coluna
  // (Spine1, Spine2, Head, Jaw); a demo é assim e a paridade também.
  for (const char* k : {"Hips", "Spine", "Spine1", "Spine2", "Neck", "Head", "LeftArm", "RightArm", "LeftForeArm", "RightForeArm",
                        "LeftUpLeg", "RightUpLeg", "LeftLeg", "RightLeg", "LeftFoot", "RightFoot", "Jaw"}) {
    const int b = rig.bone(k);
    if (b < 0) continue;
    bool animated = false;
    for (const auto& [key, clip] : rig.clips)
      for (const Track& t : clip.tracks) animated = animated || (t.node == b && t.path == Track::Path::Position);
    if (!animated) zeroed_.push_back(b);
  }
  if (idle_ >= 0) {
    mixer_.action(idle_).weight = 1;
    mixer_.action(idle_).time = rand() * mixer_.action(idle_).clip->duration;
  }
  phase_ = rand() * 6;
  pose_ = rig_.rest;
  for (int b : zeroed_) pose_.t[static_cast<std::size_t>(b)] = glm::vec3(0.0f);
  mixer_.update(0, pose_);
}

glm::mat4 QuadrupedAnimator::bodyMatrix() const {
  return glm::scale(glm::translate(glm::mat4(1.0f), bodyPos_) * eulerXYZ(bodyRot_), bodyScale_);
}

// stepQuadruped: ocioso ↔ caminhada pela velocidade; derrubada, o ocioso congela
void QuadrupedAnimator::step(double dt, double mps, double base, double maxTs, bool frozen) {
  walkW_ = approach(walkW_, !frozen && mps > 0.4 ? 1 : 0, dt * 6);
  if (walk_ >= 0) {
    mixer_.action(walk_).timeScale = clamp(mps / (base * scale_), 0.6, maxTs);
    mixer_.action(walk_).weight = walkW_;
  }
  if (idle_ >= 0) {
    mixer_.action(idle_).timeScale = frozen ? 0 : 1;
    mixer_.action(idle_).weight = 1 - walkW_;
  }
  pose_ = rig_.rest;
  for (int b : zeroed_) pose_.t[static_cast<std::size_t>(b)] = glm::vec3(0.0f);
  mixer_.update(dt, pose_);
}

// postureQuadruped: inclina para dentro da curva e acompanha o declive
void QuadrupedAnimator::posture(double dt, double w, const AnimWorld& world) {
  double d = world.rootYaw - prevYaw_;
  while (d > kPi) d -= kPi * 2;
  while (d < -kPi) d += kPi * 2;
  prevYaw_ = world.rootYaw;
  const double turnRate = clamp(d / std::max(dt, 1e-3) * 0.08, -0.25, 0.25);
  lean_ = approach(lean_, turnRate * w, dt * 6);
  const double px = static_cast<double>(world.rootPos.x), pz = static_cast<double>(world.rootPos.z), sy = std::sin(world.rootYaw), cy = std::cos(world.rootYaw);
  const double rd = 1.1 * scale_;
  const auto gh = [&](double x, double z) { return world.groundHeight ? world.groundHeight(x, z) : 0.0; };
  const double fx = gh(px + sy * rd, pz + cy * rd) - gh(px - sy * rd, pz - cy * rd);
  const double rx = gh(px + cy * 0.6, pz - sy * 0.6) - gh(px - cy * 0.6, pz + sy * 0.6);
  tiltX_ = approach(tiltX_, clamp(std::atan2(fx, 2 * rd) * 0.6, -0.28, 0.28), dt * 5);
  tiltZ_ = approach(tiltZ_, clamp(std::atan2(rx, 1.2) * 0.5, -0.22, 0.22), dt * 5);
  bodyRot_.x += static_cast<float>(tiltX_);
  bodyRot_.z += static_cast<float>(lean_ - tiltZ_);
}

void QuadrupedAnimator::animateHound(const QuadrupedInput& s, double dt) {
  phase_ += dt;
  const double k = scale_, mps = s.mps;
  walkW_ = approach(walkW_, !s.down && mps > 0.4 ? 1 : 0, dt * 6);
  bodyRot_ = glm::vec3(0.0f);
  bodyPos_ = glm::vec3(0.0f);
  if (s.down) {
    // desaba de lado e assenta no chão
    downT_ = std::min(1.0, downT_ + dt * 1.8);
    const double d = downT_ * downT_ * (3 - 2 * downT_);
    bodyRot_.z = static_cast<float>(d * kPi * 0.45);
    bodyPos_.y = static_cast<float>(HOVER * k * (1 - d) + 0.18 * k * d);
    return;
  }
  downT_ = 0;
  // paira e respira; andando, abaixa a cabeça e desliza
  double y = HOVER * k + std::sin(phase_ * 2.1 + seed_) * 0.07 * k + std::sin(phase_ * 6.2) * 0.02 * k * walkW_;
  double rx = 0.16 * walkW_ * std::min(1.0, mps / 6), z = 0;
  double rz = std::sin(phase_ * 1.3 + seed_) * 0.05;
  const double pulse = 1 + std::sin(phase_ * 2.6 + seed_) * 0.025;
  bodyScale_ = glm::vec3(static_cast<float>(k * pulse), static_cast<float>(k * (2 - pulse)), static_cast<float>(k * pulse));
  if (s.windup >= 0) {  // recua e se encolhe antes do bote
    z -= 0.35 * s.windup * k;
    y -= 0.14 * s.windup * k;
    rx += 0.24 * s.windup;
  }
  if (s.attack >= 0) {  // bote: avança e ergue a frente
    const double t = std::sin(s.attack * kPi);
    z += 1.0 * t * k;
    y += 0.16 * t * k;
    rx -= 0.32 * t;
  }
  if (s.stun) rz += std::sin(phase_ * 14) * 0.25;
  bodyPos_ = glm::vec3(0.0f, static_cast<float>(y), static_cast<float>(z));
  bodyRot_ = glm::vec3(static_cast<float>(rx), 0.0f, static_cast<float>(rz));
}

bool QuadrupedAnimator::animate(const QuadrupedInput& s, const AnimWorld& world) {
  const double dt = lod_.step(s.dt > 0 ? s.dt : 0.016, world.rootPos, world.camera);
  if (!(dt > 0)) return false;
  if (kind_ == Kind::Hound) {
    animateHound(s, dt);
    if (!s.down) posture(dt, walkW_, world);
    return true;
  }
  if (kind_ == Kind::Mount) {
    step(dt, s.mps, 1.9, 3.4, false);
    bodyRot_ = glm::vec3(0.0f);
    bodyPos_ = glm::vec3(0.0f);
    posture(dt, walkW_, world);
    return true;
  }
  // leoa
  phase_ += dt;
  step(dt, s.mps, 2.5, 2.4, s.down);
  bodyRot_ = glm::vec3(0.0f);
  bodyPos_ = glm::vec3(0.0f);
  if (s.down) {
    bodyRot_.z = static_cast<float>(kPi / 2);
    bodyPos_.y = static_cast<float>(0.32 * scale_);
    return true;
  }
  if (s.windup >= 0) {  // agacha e recua antes do bote
    bodyPos_ = glm::vec3(0.0f, static_cast<float>(-0.1 * s.windup * scale_), static_cast<float>(-0.3 * s.windup));
    bodyRot_.x = static_cast<float>(0.1 * s.windup);
    bend(rig_, pose_, "Head", 1, 0, 0, 0.3 * s.windup);
  }
  if (s.attack >= 0) {  // bote: avança, ergue a frente e abre a boca
    const double t = std::sin(s.attack * kPi);
    bodyPos_.z = static_cast<float>(0.9 * t);
    bodyRot_.x = static_cast<float>(-0.22 * t);
    bend(rig_, pose_, "Head", 1, 0, 0, -0.35 * t);
    bend(rig_, pose_, "Jaw", 1, 0, 0, 0.45 * t);
  }
  if (s.stun) bend(rig_, pose_, "Head", 0, 0, 1, std::sin(phase_ * 14) * 0.25);
  posture(dt, walkW_, world);
  return true;
}

}  // namespace rpg::client::anim
