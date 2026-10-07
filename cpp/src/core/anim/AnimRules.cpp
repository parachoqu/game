#include "core/anim/AnimRules.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/JsMath.h"
#include "core/Math.h"

namespace rpg::anim {

SkillPose skillPose(std::string_view id) {
  const auto starts = [&](std::string_view p) { return id.substr(0, p.size()) == p; };
  SkillPose pose;
  if (id == "giro" || id == "ceifaCircular") pose.kind = ActionKind::Spin;
  else if (id == "investida" || id == "estocadaLonga" || id == "passoSombras") pose.kind = ActionKind::Thrust;
  else if (id == "tiroCarregado") pose.kind = ActionKind::Charge;
  else if (id == "recuo") pose.kind = ActionKind::Bow;
  else if (id == "tiroBesta" || id == "disparoRepulsao") pose.kind = ActionKind::Crossbow;
  else if (id == "comboManopla" || id == "avancoImpacto") pose.kind = ActionKind::Rapid;
  else if (id == "machadadaPesada" || id == "golpeEsmagador" || id == "ondaChoque") pose.kind = ActionKind::Heavy;
  else if (id == "dilacerar" || id == "golpePreciso" || id == "varrerDistancia" || id == "puxaoFoice") pose.kind = ActionKind::Slash;
  else if (starts("seta") || starts("bola") || starts("erupcao") || starts("prisao") || starts("enraizar") ||
           starts("lufada") || starts("marca") || starts("cura") || starts("escudo") || starts("purificacao"))
    pose.kind = ActionKind::Cast;
  static constexpr std::pair<std::string_view, double> kDur[] = {
      {"investida", 0.55}, {"giro", 0.75}, {"tiroCarregado", 0.85}, {"recuo", 0.5}, {"atordoar", 0.85},
      {"comboManopla", 0.55}, {"avancoImpacto", 0.6}, {"golpeEsmagador", 0.7}, {"ondaChoque", 0.75},
      {"machadadaPesada", 0.75}, {"dilacerar", 0.6}, {"passoSombras", 0.45}, {"golpePreciso", 0.45},
      {"estocadaLonga", 0.6}, {"varrerDistancia", 0.65}, {"ceifaCircular", 0.75}, {"puxaoFoice", 0.6},
      {"tiroBesta", 0.7}, {"disparoRepulsao", 0.55}, {"setaGelo", 0.55}, {"prisaoGelo", 0.7},
      {"bolaFogo", 0.65}, {"erupcaoFogo", 0.75}, {"enraizar", 0.6}, {"bencaoTerra", 0.55},
      {"lufadaVento", 0.55}, {"saltoVento", 0.5}, {"marcaCorruptora", 0.6}, {"drenoVital", 0.7},
      {"formaFera", 0.6}, {"rugidoFera", 0.65}, {"curaVital", 0.6}, {"auraRestauracao", 0.65},
      {"escudoRadiante", 0.55}, {"purificacaoSagrada", 0.7},
  };
  for (const auto& [k, d] : kDur)
    if (k == id) pose.duration = d;
  if (id == "giro" || id == "ceifaCircular") pose.spinStart = 0.35;
  return pose;
}

double skillProgress(const SkillPose& pose, double t) {
  if (pose.spinStart > 0) return t < pose.spinStart ? 0 : std::min(1.0, (t - pose.spinStart) / 0.4);
  return std::min(1.0, t / pose.duration);
}

LocalDir toLocal(Real yaw, Real dx, Real dz) {
  const Real s = js::sin(yaw), c = js::cos(yaw);
  return {dx * s + dz * c, dx * c - dz * s};
}

std::string_view clipKey(Clip c) {
  static constexpr std::array<std::string_view, static_cast<std::size_t>(Clip::Count)> kKeys = {
      "walk", "walkBack", "strafeLeft", "strafeRight",
      "dodgeForward", "dodgeBackward", "dodgeLeft", "dodgeRight", "roll",
      "hitFront", "hitBack", "hitLeft", "hitRight",
      "deathForward", "deathBackward", "down",
  };
  return kKeys[static_cast<std::size_t>(c)];
}

LocomotionWeights locomotionWeights(Real fwd, Real left, const HasClip& has) {
  LocomotionWeights w;
  const Real L = jsHypot(fwd, left);
  if (L < 1e-4) return w;
  const Real f = fwd / L, l = left / L;
  w.walk = std::max(0.0, f);
  w.walkBack = std::max(0.0, -f);
  w.strafeLeft = std::max(0.0, l);
  w.strafeRight = std::max(0.0, -l);
  if (!has(Clip::WalkBack)) {
    w.reverse = f < -0.15;
    w.walk += w.walkBack;
    w.walkBack = 0.0;
  }
  if (!has(Clip::StrafeLeft)) {
    w.walk += w.strafeLeft;
    w.strafeLeft = 0.0;
  }
  if (!has(Clip::StrafeRight)) {
    w.walk += w.strafeRight;
    w.strafeRight = 0.0;
  }
  const Real sum = w.walk + w.walkBack + w.strafeLeft + w.strafeRight;
  w.walk /= sum;
  w.walkBack /= sum;
  w.strafeLeft /= sum;
  w.strafeRight /= sum;
  return w;
}

std::optional<Clip> dodgeClip(Real fwd, Real left, const HasClip& has, bool allow) {
  Clip key;
  if (std::fabs(fwd) >= std::fabs(left)) key = fwd >= 0 ? Clip::DodgeForward : Clip::DodgeBackward;
  else key = left > 0 ? Clip::DodgeLeft : Clip::DodgeRight;
  if (allow && has(key)) return key;
  if (has(Clip::Roll)) return Clip::Roll;
  return std::nullopt;
}

Real dodgeFacing(Clip c) {
  switch (c) {
    case Clip::DodgeLeft: return kPi / 2;
    case Clip::DodgeRight: return -kPi / 2;
    case Clip::DodgeBackward: return kPi;
    default: return 0.0;
  }
}

HitSide hitSide(Real victimYaw, Real vx, Real vz, const std::optional<HitOrigin>& from) {
  if (!from || !std::isfinite(from->x) || !std::isfinite(from->z)) return HitSide::Front;
  const LocalDir d = toLocal(victimYaw, from->x - vx, from->z - vz);
  if (std::fabs(d.fwd) < 1e-6 && std::fabs(d.left) < 1e-6) return HitSide::Front;
  if (std::fabs(d.fwd) >= std::fabs(d.left)) return d.fwd >= 0 ? HitSide::Front : HitSide::Back;
  return d.left > 0 ? HitSide::Left : HitSide::Right;
}

std::optional<Clip> hitClip(HitSide side, const HasClip& has) {
  Clip key = Clip::HitFront;
  switch (side) {
    case HitSide::Front: key = Clip::HitFront; break;
    case HitSide::Back: key = Clip::HitBack; break;
    case HitSide::Left: key = Clip::HitLeft; break;
    case HitSide::Right: key = Clip::HitRight; break;
  }
  if (has(key)) return key;
  return std::nullopt;
}

DeathFall deathFall(Real victimYaw, Real vx, Real vz, const std::optional<HitOrigin>& from) {
  if (!from || !std::isfinite(from->x) || !std::isfinite(from->z)) return DeathFall::Backward;
  return toLocal(victimYaw, from->x - vx, from->z - vz).fwd >= 0 ? DeathFall::Backward : DeathFall::Forward;
}

std::optional<Clip> deathClip(DeathFall fall, const HasClip& has) {
  const Clip key = fall == DeathFall::Forward ? Clip::DeathForward : Clip::DeathBackward;
  if (has(key)) return key;
  if (has(Clip::Down)) return Clip::Down;
  return std::nullopt;
}

}  // namespace rpg::anim
