#include "client/game/ThirdPersonCamera.h"

#include <algorithm>
#include <cmath>

#include "core/Math.h"
#include "core/world/StaticWorld.h"

namespace rpg::client {

namespace {

double k(double rate, double dt) { return 1.0 - std::exp(-rate * dt); }

}  // namespace

void ThirdPersonCamera::setPreset(int n) {
  if (n == 1) targetDist_ = 20;
  else if (n == 2) targetDist_ = 13;
  else if (n == 3) targetDist_ = 7.5;
}

void ThirdPersonCamera::setFixed(const glm::dvec3& pos, const glm::dvec3& look, double fovDeg) {
  view_.position = pos;
  view_.target = look;
  look_ = look;
  view_.fovDeg = fovDeg;
  hasPose_ = false;  // o próximo update volta com snap
}

void ThirdPersonCamera::update(double dt, bool snap, const CameraSubject& P, const InputState& in, bool uiOpen,
                               const StaticMap& map) {
  if (!hasPose_) snap = true;
  hasPose_ = true;
  if (freeCam_) {
    view_.position = freeCam_->first;
    view_.target = freeCam_->second;
    return;
  }
  const bool aiming = P.aiming && !uiOpen;
  aimT_ = snap ? (aiming ? 1 : 0) : aimT_ + ((aiming ? 1 : 0) - aimT_) * std::min(1.0, dt * 9);
  const double a = aimT_;

  // amortecimento do recuo e do tremor
  recoilPitch_ *= std::max(0.0, 1 - dt * 14);
  recoilYaw_ *= std::max(0.0, 1 - dt * 14);
  shake_ = std::max(0.0, shake_ - dt * 2.8);

  // alternância suave de ombro (V)
  curShoulder_ = snap ? shoulder_ : curShoulder_ + (shoulder_ - curShoulder_) * std::min(1.0, dt * 8);

  if (!uiOpen) {
    targetDist_ = clamp(targetDist_ + in.wheel * 1.5, 6, 22);
    dist_ += (targetDist_ - dist_) * (snap ? 1 : k(11, dt));

    // mirando, a sensibilidade cai com o campo de visão (mira menos arisca)
    const double sens = prefs.sensFactor() * (1 - a * 0.35), inv = prefs.invert ? -1 : 1;
    const double turn = (in.down(Key::Right) ? 1 : 0) - (in.down(Key::Left) ? 1 : 0);
    const double tilt = (in.down(Key::Down) ? 1 : 0) - (in.down(Key::Up) ? 1 : 0);
    const bool looking = in.locked || aiming;
    const double dx = in.dragDX + (dragging_ ? in.leftDX : 0) + (looking ? in.lookDX : 0);
    const double dy = in.dragDY + (dragging_ ? in.leftDY : 0) + (looking ? in.lookDY : 0);
    if (turn != 0 || tilt != 0 || dx != 0 || dy != 0) resetting_ = false;
    yaw_ -= (turn * dt * 1.9 + dx * 0.0052) * sens;
    pitch_ = clamp(pitch_ + (tilt * dt * 1.1 + dy * 0.0045) * sens * inv, kPitchMin, kPitchMax);

    // sem o ponteiro preso, perto da borda da tela o cursor empurra a câmera
    if (aiming && !in.locked && in.width > 0 && in.height > 0) {
      const double ex = in.rawX / in.width - 0.5, ey = in.rawY / in.height - 0.5;
      const double overX = std::abs(ex) - 0.44, overY = std::abs(ey) - 0.44;
      if (overX > 0) {
        yaw_ -= (ex < 0 ? -1 : 1) * overX * dt * 26 * sens;
        resetting_ = false;
      }
      if (overY > 0) {
        pitch_ = clamp(pitch_ + (ey < 0 ? -1 : 1) * overY * dt * 16 * sens * inv, kPitchMin, kPitchMax);
        resetting_ = false;
      }
    }
  }

  if (resetting_) {  // botão "N" do minimapa: volta ao norte e à inclinação padrão
    const double target = std::round(yaw_ / kTau) * kTau;
    const double kr = k(8, dt);
    yaw_ += (target - yaw_) * kr;
    pitch_ += (kPitchDefault - pitch_) * kr;
    if (std::abs(target - yaw_) < 0.002 && std::abs(kPitchDefault - pitch_) < 0.002) {
      yaw_ = 0;
      pitch_ = kPitchDefault;
      resetting_ = false;
    }
  }

  const bool sky = skyDir_.has_value();
  const double fov = sky ? 26 : kFovBase + (kAimFov - kFovBase) * a;
  const double fovK = sky || (a < 0.01 && view_.fovDeg < 40) ? 3 : 9;
  if (std::abs(view_.fovDeg - fov) > 0.05) view_.fovDeg += (fov - view_.fovDeg) * std::min(1.0, snap ? 1 : dt * fovK);

  if (sky) {
    const glm::dvec3 camPos(P.pos.x, P.pos.y + 14, P.pos.z);
    view_.position += (camPos - view_.position) * (snap ? 1 : std::min(1.0, dt * 2));
    const glm::dvec3 lookAt = view_.position + *skyDir_ * 100.0;
    look_ += (lookAt - look_) * (snap ? 1 : std::min(1.0, dt * 1.6));
    view_.target = look_;
    return;
  }

  // mirando: inclinação própria, preservando o alcance vertical
  const double pAim = clamp(pitch_ * 0.72 + 0.08, kAimPitchMin, kAimPitchMax);
  const double pitch = pitch_ + (pAim - pitch_) * a;
  const double dist = dist_ + (kAimDist - dist_) * a;
  const double effPitch = pitch + recoilPitch_;
  const double effYaw = yaw_ + recoilYaw_;
  const double orbit = std::max(effPitch, kOrbitMin);
  const double lift = std::tan(std::max(0.0, kOrbitMin - effPitch)) * dist;

  // o alvo acompanha na hora na horizontal e com atraso na vertical (degraus não sacodem a imagem)
  const double alvoY = P.pos.y - P.airY * 0.5 + (P.mounted ? 3 : 1.7 - 0.55 * P.crouchW);
  if (snap || !tgtY_) tgtY_ = alvoY;
  else *tgtY_ += (alvoY - *tgtY_) * k(std::abs(alvoY - *tgtY_) > 2 ? 16 : 6, dt);
  // correndo, a câmera olha um pouco à frente
  const double quer = (1 - a) * clamp((P.speedNow - kWalk * 0.6) / (kRun - kWalk * 0.6), 0, 1) * 1.1;
  lead_ = snap ? quer : lead_ + (quer - lead_) * k(3.5, dt);
  glm::dvec3 camTarget(P.pos.x + std::sin(P.yaw) * lead_, *tgtY_, P.pos.z + std::cos(P.yaw) * lead_);
  if (a > 0.001) {  // por cima do ombro
    camTarget.x += std::cos(effYaw) * (kAimSide * curShoulder_) * a;
    camTarget.z -= std::sin(effYaw) * (kAimSide * curShoulder_) * a;
    camTarget.y += 0.14 * a;
  }

  // spring arm: não atravessa relevo, construções, rochas nem muros
  const double co = std::cos(orbit), so = std::sin(orbit);
  const glm::dvec3 dir(std::sin(effYaw) * co, so, std::cos(effYaw) * co);
  double clearDist = dist;
  constexpr int kProbeSteps = 12;
  for (int step = 1; step <= kProbeSteps; ++step) {
    const double testD = static_cast<double>(step) / kProbeSteps * dist;
    const glm::dvec3 pt = camTarget + dir * testD;
    const double gh = map.groundHeight(pt.x, pt.z) + 0.65;
    if (pt.y < gh || map.collision().testPoint(pt.x, pt.z, 0.42)) {
      clearDist = std::max(1.8, testD - 0.45);
      break;
    }
  }
  // entra depressa (o muro chegou), volta devagar (não pula de volta ao passar da quina)
  if (snap || actualDist_ == 0) actualDist_ = clearDist;
  else actualDist_ = lerp(actualDist_, clearDist, k(clearDist < actualDist_ ? 25 : 6.5, dt));

  glm::dvec3 camPos = camTarget + dir * actualDist_;
  const double minFloor = map.groundHeight(camPos.x, camPos.z) + 0.55;
  if (camPos.y < minFloor) camPos.y = minFloor;

  if (shake_ > 0.01) {
    const double sk = shake_ * shake_ * 0.16;
    std::uniform_real_distribution<double> u(-0.5, 0.5);
    camPos += glm::dvec3(u(shakeRng_), u(shakeRng_), u(shakeRng_)) * sk;
  }

  glm::dvec3 lookAt = camTarget;
  lookAt.y += lift;
  view_.position += (camPos - view_.position) * (snap ? 1 : k(7.5 + 6.5 * a, dt));
  look_ += (lookAt - look_) * (snap ? 1 : k(10 + 8 * a, dt));
  view_.target = look_;
}

}  // namespace rpg::client
