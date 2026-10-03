#include "core/movement/CharacterMotor.h"

#include <cmath>

#include "core/JsMath.h"
#include "core/Math.h"
#include "core/movement/MoveEntity.h"

namespace rpg::motor {

double moveSpeed(const Balance::Movement& m, const SpeedInputs& in, double& landT, double dt) {
  double sp = (in.running ? m.run : m.walk) * in.armorSpeed;
  if (in.crouch) sp *= m.crouch;
  if (landT > 0) {
    landT -= dt;
    sp *= kLandSlow;
  }
  if (in.aiming) sp *= kAimSlow;
  if (in.metamorph) sp *= kBeastFast;
  if (in.load == LoadState::Overloaded) sp *= kOverloadSlow;
  else if (in.load == LoadState::Limit) sp *= kLimitSlow;
  if (in.mounted) sp = in.load == LoadState::Normal ? kMountedSpeed : kMountedLoaded;
  if (in.wading) sp *= kWadeSlow;
  return sp;
}

Wading wadingAt(const StaticMap& map, double x, double z) {
  Wading w;
  w.surface = map.waterAt(x, z);
  w.wading = w.surface - map.groundHeight(x, z) > kWadeDepth;
  return w;
}

Air startJump(const Balance::Movement& m, bool boosted, double mx, double mz, double sp, double yaw, double armorSpeed) {
  const double vy = boosted ? m.boost.vy : m.jump.vy;
  double vx = mx * sp, vz = mz * sp;
  if (boosted) {
    // `mx || mz`: zero (e -0) conta como falso
    const bool dirGiven = mx != 0.0 || mz != 0.0;
    const double dx = dirGiven ? mx : js::sin(yaw), dz = dirGiven ? mz : js::cos(yaw);
    const double boostSpeed = m.boost.speed * armorSpeed;
    const double speed = sp > boostSpeed ? sp : boostSpeed;  // Math.max(sp, …)
    vx = dx * speed;
    vz = dz * speed;
  }
  Air a;
  a.y = 0;
  a.vy = vy;
  a.vx = vx;
  a.vz = vz;
  a.boosted = boosted;
  a.t = 0;
  a.dur = 2 * vy / m.gravity;
  return a;
}

void airSteer(const Balance::Movement& m, const StaticMap& map, Air& a, XZ& pos, bool moving, double mx, double mz,
              double sp, double dt) {
  const double pull = 1 - js::exp(-m.airControl * 4 * dt);
  if (moving) {
    a.vx += (mx * sp - a.vx) * pull;
    a.vz += (mz * sp - a.vz) * pull;
  }
  moveEntity(map, pos, a.vx * dt, a.vz * dt, kBodyRadius, a.y);
}

bool applyGravity(const Balance::Movement& m, std::optional<Air>& air, double& landT, double dt) {
  if (!air) return false;
  Air& a = *air;
  a.t += dt;
  a.vy -= m.gravity * dt;
  a.y += a.vy * dt;
  if (a.y <= 0 && a.vy < 0) {
    landT = a.boosted ? kLandBoosted : kLandNormal;
    air.reset();
    return true;
  }
  return false;
}

double bodyHeight(const StaticMap& map, double x, double z, const std::optional<Air>& air, const Wading& w) {
  double y = map.groundHeight(x, z) + (air ? air->y : 0.0);
  if (w.wading) {
    const double floor = w.surface - kWadeFloat;
    y = y > floor ? y : floor;  // Math.max
  }
  return y;
}

}  // namespace rpg::motor
