#include "server/CommandValidator.h"

#include <algorithm>
#include <cmath>

namespace rpg::server {

namespace proto = rpg::protocol;

bool CommandValidator::refill(double& tokens, double& last, double rate, double burst, double now) {
  if (last >= 0) tokens = std::min(burst, tokens + std::max(0.0, now - last) * rate);
  last = now;
  if (tokens < 1.0) return false;
  tokens -= 1.0;
  return true;
}

bool CommandValidator::command(proto::PlayerCommand& cmd, double px, double py, double pz, double now) {
  if (!refill(cmdTokens_, cmdLast_, limits_.commandsPerSecond, limits_.commandBurst, now)) {
    ++dropped_;
    return false;
  }
  bool changed = false;
  const auto clampAxis = [&](std::int8_t& v) {
    const auto c = static_cast<std::int8_t>(std::clamp<int>(v, -1, 1));
    if (c != v) changed = true;
    v = c;
  };
  clampAxis(cmd.moveForward);
  clampAxis(cmd.moveRight);
  const auto finite = [&](double& v, double fallback) {
    if (std::isfinite(v)) return;
    v = fallback;
    changed = true;
  };
  finite(cmd.camYaw, 0);
  finite(cmd.aimYaw, 0);
  finite(cmd.aimPitch, 0);
  finite(cmd.aimDist, 0);
  finite(cmd.aimX, px);
  finite(cmd.aimY, py);
  finite(cmd.aimZ, pz);
  const std::uint16_t known = proto::kHeldRun | proto::kHeldCrouch | proto::kHeldAim | proto::kHeldUiOpen;
  if (cmd.held & ~known) {
    cmd.held &= known;
    changed = true;
  }
  if (cmd.aimDist < 0 || cmd.aimDist > limits_.aimRange) {
    cmd.aimDist = std::clamp(cmd.aimDist, 0.0, limits_.aimRange);
    changed = true;
  }
  // ponto mirado longe demais: puxado para dentro do alcance, na mesma direção
  const double dx = cmd.aimX - px, dy = cmd.aimY - py, dz = cmd.aimZ - pz;
  const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (d > limits_.aimRange) {
    const double k = limits_.aimRange / d;
    cmd.aimX = px + dx * k;
    cmd.aimY = py + dy * k;
    cmd.aimZ = pz + dz * k;
    changed = true;
  }
  if (changed) ++fixed_;
  return true;
}

bool CommandValidator::request(double now) {
  if (refill(reqTokens_, reqLast_, limits_.requestsPerSecond, limits_.requestBurst, now)) return true;
  ++dropped_;
  return false;
}

}  // namespace rpg::server
