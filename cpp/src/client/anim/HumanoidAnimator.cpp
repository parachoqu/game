#include "client/anim/HumanoidAnimator.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace rpg::client::anim {

namespace {

using rpg::anim::ActionKind;

constexpr double kPi = 3.141592653589793;
constexpr double BASE_MPS = 1.05;          // velocidade que a caminhada cobre sozinha, a 1x
constexpr double CROUCH_CLIP_MPS = 1.2;
constexpr double DODGE_MOVE = 0.34 / 0.42;
constexpr double HIT_RATE = 1.4, DEATH_RATE = 1.25, POSTURE_RATE = 1.3, DODGE_TAIL = 0.25;
constexpr double BOW_RELEASE_AT = 0.28, BOW_TWIST = -1.83;
struct BowTimes {
  double draw = 2.05, redraw = 2.3, full = 2.8, hold = 3.6, release = 3.93, after = 4.2;
};
constexpr BowTimes BOW{};
constexpr std::array<const char*, 3> SIDE_CYCLES = {"walkBack", "strafeLeft", "strafeRight"};
constexpr std::array<const char*, 11> CLIPS_TO_ZERO = {"slash", "thrust", "punch", "smash", "bow", "cast", "spell_area", "roll", "down", "roar", "craft"};

double clamp(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
double approach(double v, double t, double k) { return v + (t - v) * std::min(1.0, k); }

bool upperSkip(std::string_view key) {
  static constexpr std::array<std::string_view, 11> kSkip = {"Hips", "LeftUpLeg", "RightUpLeg", "LeftLeg", "RightLeg", "LeftFoot",
                                                             "RightFoot", "LeftToeBase", "RightToeBase", "LeftToe_End", "RightToe_End"};
  return std::find(kSkip.begin(), kSkip.end(), key) != kSkip.end();
}

}  // namespace

HumanoidAnimator::HumanoidAnimator(const Rig& rig, const ClipMetaMap& meta, glm::vec2 race, double scale, std::uint32_t seed,
                                   std::function<double()> random)
    : rig_(rig), meta_(meta), mixer_(rig), pose_(rig.rest), rng_(seed), random_(std::move(random)), race_(race), scale_(scale) {
  // makeHumanoid: uma ação por clipe, na ordem do molde, todas tocando com peso zero
  for (const std::string& key : rig.clipOrder) {
    const Clip* c = rig.clip(key);
    if (!c) continue;
    actions_[key] = mixer_.add(c);
  }
  // clipes do lote de combate: o instante é escrito a cada quadro (o mixer não avança)
  for (const auto& [key, a] : actions_) {
    const auto m = meta_.find(key);
    if (m == meta_.end() || m->second.group != "combat") continue;
    mixer_.action(a).timeScale = 0;
    if (m->second.locomotionMode != "cycle") oneShots_.push_back(a);
  }
  idle_ = act("idle");
  walk_ = act("walk");
  if (idle_ >= 0) mixer_.action(idle_).weight = 1;
  if (walk_ >= 0) mixer_.action(walk_).time = rand() * duration(walk_);
  walkDur_ = walk_ >= 0 && duration(walk_) > 0 ? duration(walk_) : 1.333;
  const int run = act("run");
  runDur_ = run >= 0 ? duration(run) : 0.542;
  phase_ = rand() * 6;
  glanceTimer_ = 1.5 + rand() * 2.5;
  hipsY_ = rig.hipsY * static_cast<double>(race.y) * scale;
  // pose ociosa antes de qualquer quadro (mixerStep(h, 0))
  pose_ = rig_.rest;
  mixer_.update(0, pose_);
}

int HumanoidAnimator::act(std::string_view key) const {
  const auto it = actions_.find(std::string(key));
  return it == actions_.end() ? -1 : it->second;
}

Action* HumanoidAnimator::A(std::string_view key) {
  const int a = act(key);
  return a < 0 ? nullptr : &mixer_.action(a);
}

void HumanoidAnimator::setWeapon(const std::string& family) { family_ = family; }

void HumanoidAnimator::overlay(int a, double w) {
  if (w > 0.001)
    for (std::size_t i = 0; i < mixer_.size(); ++i)
      if (static_cast<int>(i) != a) mixer_.action(static_cast<int>(i)).weight *= (1 - w);
  mixer_.action(a).weight = std::max(0.0, w);
}

void HumanoidAnimator::bend(std::string_view key, double x, double y, double z, double angle) { anim::bend(rig_, pose_, key, x, y, z, angle); }

void HumanoidAnimator::ampBone(std::string_view key, double f) {
  const int b = rig_.bone(key);
  const auto ref = rig_.idleRef.find(key);
  if (b < 0 || ref == rig_.idleRef.end() || std::abs(f - 1) < 0.001) return;
  pose_.r[static_cast<std::size_t>(b)] = slerpThree(ref->second, pose_.r[static_cast<std::size_t>(b)], f);
}

// tronco e braços de um clipe por cima da locomoção (quadril e pernas ficam de fora)
void HumanoidAnimator::upperLayer(std::string_view key, double t, double w) {
  const Clip* c = rig_.clip(key);
  if (!c) return;
  float v[4];
  for (const Track& tr : c->tracks) {
    if (tr.path != Track::Path::Quaternion) continue;
    const auto n = static_cast<std::size_t>(tr.node);
    if (!rig_.isBone[n] || upperSkip(boneKey(rig_.names[n]))) continue;
    sampleTrack(tr, t, v);
    pose_.r[n] = slerpThree(pose_.r[n], glm::quat(v[3], v[0], v[1], v[2]), w);
  }
}

glm::mat4 HumanoidAnimator::bodyMatrix() const {
  glm::mat4 m = glm::translate(glm::mat4(1.0f), bodyPos_) * eulerXYZ(bodyRot_);
  const auto sx = static_cast<float>(static_cast<double>(race_.x) * scale_), sy = static_cast<float>(static_cast<double>(race_.y) * scale_);
  return glm::scale(m, glm::vec3(sx, sy, sx));
}

// inclina o corpo na curva e no declive; pescoço e cabeça compensam
void HumanoidAnimator::postura(double dt, double w, const AnimWorld& world) {
  double d = world.rootYaw - prevYaw_;
  while (d > kPi) d -= kPi * 2;
  while (d < -kPi) d += kPi * 2;
  prevYaw_ = world.rootYaw;
  const double turnRate = clamp(d / std::max(dt, 1e-3) * 0.055, -0.22, 0.22);
  lean_ = approach(lean_, turnRate * w, dt * 5);
  const double px = static_cast<double>(world.rootPos.x), pz = static_cast<double>(world.rootPos.z), sy = std::sin(world.rootYaw), cy = std::cos(world.rootYaw), r = 0.55;
  const auto gh = [&](double x, double z) { return world.groundHeight ? world.groundHeight(x, z) : 0.0; };
  const double fx = gh(px + sy * r, pz + cy * r) - gh(px - sy * r, pz - cy * r);
  const double rx = gh(px + cy * r, pz - sy * r) - gh(px - cy * r, pz + sy * r);
  tiltX_ = approach(tiltX_, clamp(std::atan2(fx, 2 * r) * 0.45, -0.18, 0.18), dt * 4.5);
  tiltZ_ = approach(tiltZ_, clamp(std::atan2(rx, 2 * r) * 0.45, -0.18, 0.18), dt * 4.5);
  bodyRot_.x += static_cast<float>(tiltX_);
  bodyRot_.z += static_cast<float>(lean_ - tiltZ_);
  bend("Neck", 1, 0, 0, -tiltX_ * 0.4);
  bend("Head", 1, 0, 0, -tiltX_ * 0.25);
  bend("Neck", 0, 0, 1, -(lean_ - tiltZ_) * 0.45);
  bend("Head", 0, 0, 1, -(lean_ - tiltZ_) * 0.3);
}

bool HumanoidAnimator::animate(const HumanoidInput& s, const AnimWorld& world) {
  const double dt = lod_.step(s.dt > 0 ? s.dt : 0.016, world.rootPos, world.camera);
  if (!(dt > 0)) return false;
  phase_ += dt * 4;
  const auto has = [&](std::string_view k) { return act(k) >= 0; };
  const auto hasClip = [&](rpg::anim::Clip c) { return has(rpg::anim::clipKey(c)); };
  const auto weight = [&](int a) -> double& { return mixer_.action(a).weight; };
  for (int a : oneShots_) weight(a) = 0;

  // morte direcional (inimigos): separada da derrubada recuperável
  if (s.death) {
    if (!deathKey_) {
      const auto c = rpg::anim::deathClip(s.death->fall, hasClip);
      deathKey_ = c ? std::string(rpg::anim::clipKey(*c)) : std::string();
    }
  } else {
    deathKey_.reset();
  }
  const int deathAct = deathKey_ && !deathKey_->empty() && *deathKey_ != "down" ? act(*deathKey_) : -1;
  const bool down = s.down || s.death.has_value();
  const bool dodging = s.dodge >= 0;

  // inércia e aceleração
  const double prevMps = mps_;
  mps_ = approach(mps_, s.mps, dt * 9);
  const double mps = mps_;
  const double rawAccel = (mps - prevMps) / std::max(dt, 1e-3);
  smoothAccel_ = approach(smoothAccel_, clamp(rawAccel, -18, 18), dt * 8);

  double targetFwd = 1, targetStrafe = 0;
  if (s.fwd) {
    targetFwd = *s.fwd;
    targetStrafe = s.strafe;
  } else if (s.moveDir) {
    const double yaw = world.rootYaw;
    const double mx = static_cast<double>(s.moveDir->x), mz = static_cast<double>(s.moveDir->y);
    targetFwd = mx * std::sin(yaw) + mz * std::cos(yaw);
    targetStrafe = mx * std::cos(yaw) - mz * std::sin(yaw);
  }
  fwd_ = approach(fwd_, targetFwd, dt * 9);
  strafe_ = approach(strafe_, targetStrafe, dt * 8);

  const bool moving = !s.mounted && !down && !dodging && mps > 0.35;
  walkW_ = approach(walkW_, moving ? 1 : 0, dt * 8);
  const double runK = clamp((mps - 3.4) / 2.6, 0, 1);
  const double stride = 1 + 0.35 * clamp((mps - 1) / 2.6, 0, 1);

  // direção da passada: frente pela caminhada (e corrida); para trás e de lado pelos clipes do lote
  const rpg::anim::LocomotionWeights dir = rpg::anim::locomotionWeights(fwd_, strafe_, hasClip);
  const auto dirOf = [&](std::string_view k) {
    return k == "walkBack" ? dir.walkBack : k == "strafeLeft" ? dir.strafeLeft : k == "strafeRight" ? dir.strafeRight : dir.walk;
  };
  const double moveSign = dir.reverse ? -1 : 1;
  const double crouchSign = fwd_ < -0.15 ? -1 : 1;
  const auto cycleOf = [&](std::string_view k) -> const PackCycle* {
    const auto it = rig_.cycle.find(k);
    return it == rig_.cycle.end() ? nullptr : &it->second;
  };
  const PackCycle* cycWalk = cycleOf("walk");
  double walkTs = clamp(mps / (BASE_MPS * stride), 0.7, 2.6);
  if (dir.walk < 0.999 && cycWalk) {
    double T = dir.walk * walkDur_ / walkTs;
    for (const char* k : SIDE_CYCLES)
      if (const PackCycle* c = cycleOf(k); dirOf(k) > 0 && c) T += dirOf(k) * c->dur / clamp(mps / c->speed, 0.6, 2.6);
    walkTs = walkDur_ / T;
  }
  if (walk_ >= 0) mixer_.action(walk_).timeScale = moveSign * walkTs;
  if (Action* run = A("run")) {
    run->timeScale = moveSign * clamp(mps / 4.6, 0.8, 2.0);
    if (prevRunK_ <= 0.05 && runK > 0.05 && walk_ >= 0) {
      const double wt = mixer_.action(walk_).time;
      const double normPhase = std::fmod(std::fmod(wt, walkDur_) + walkDur_, walkDur_) / walkDur_;
      run->time = normPhase * runDur_;
    }
    run->weight = walkW_ * runK * dir.walk;
    if (walk_ >= 0) weight(walk_) = walkW_ * (1 - runK) * dir.walk;
  } else if (walk_ >= 0) {
    weight(walk_) = walkW_ * dir.walk;
  }
  prevRunK_ = runK;
  // para trás e de lado seguem a fase da caminhada, alinhados pelo contato do pé esquerdo
  if (cycWalk && walk_ >= 0) {
    const Action& w = mixer_.action(walk_);
    double ph = std::fmod((w.time + dt * w.timeScale) / walkDur_, 1.0);
    if (ph < 0) ph += 1;
    if (moveSign < 0) ph = 1 - ph;
    for (const char* k : SIDE_CYCLES) {
      Action* a = A(k);
      if (!a) continue;
      a->weight = walkW_ * dirOf(k);
      if (const PackCycle* c = cycleOf(k)) a->time = std::fmod(std::fmod(ph - cycWalk->off + c->off, 1.0) + 1, 1.0) * c->dur;
    }
  }
  const double idleW = 1 - walkW_;
  const bool inGuard = !family_.empty() && family_ != "punhos" && mps < 0.35 && !down && s.dodge < 0;
  if (Action* guard = A("guard")) {
    guard->weight = idleW * (inGuard ? 0.85 : 0);
    if (idle_ >= 0) weight(idle_) = idleW * (inGuard ? 0.15 : 1);
  } else if (idle_ >= 0) {
    weight(idle_) = idleW;
  }

  // agachar e pular
  crouchW_ = approach(crouchW_, s.crouch ? 1 : 0, dt * 7);
  airW_ = approach(airW_, s.air ? 1 : 0, dt * 14);
  const bool crouchClips = has("crouchIdle") || has("crouchWalk");
  const double cw = crouchClips ? crouchW_ : 0;
  Action* jumpAct = s.air && s.air->boosted && A("jumpBoost") ? A("jumpBoost") : A("jump");
  const double aw0 = jumpAct ? airW_ : 0;
  const double keep = (1 - cw) * (1 - aw0);
  if (keep < 0.999)
    for (const char* k : {"idle", "walk", "run", "guard", "walkBack", "strafeLeft", "strafeRight"})
      if (Action* a = A(k)) a->weight *= keep;
  if (Action* a = A("crouchIdle")) a->weight = idleW * cw * (1 - aw0);
  if (Action* a = A("crouchWalk")) {
    a->weight = walkW_ * cw * (1 - aw0);
    a->timeScale = crouchSign * clamp(mps / CROUCH_CLIP_MPS, 0.6, 1.8);
  }
  for (const char* k : {"jump", "jumpBoost"})
    if (Action* a = A(k); a && a != jumpAct) a->weight = 0;
  if (jumpAct) {
    jumpAct->weight = aw0;
    if (s.air) jumpAct->time = clamp(s.air->phase, 0, 1) * jumpAct->clip->duration;
  }

  // mira com o arco: ergue, puxa, segura e solta a corda junto com a flecha
  Action* bowAct = A("bowAim");
  const bool bowOn = bowAct && family_ == "arco" && s.aim > 0.01 && !down && !dodging && !s.air && !s.mounted;
  bowW_ = approach(bowW_, bowOn ? s.aim : 0, dt * 10);
  bool bowShot = false;
  if (bowAct) {
    if (bowW_ > 0.001) {
      if (s.attack >= 0 && (s.kind == ActionKind::Bow || s.kind == ActionKind::Charge)) {
        if (!bowShotFrom_) bowShotFrom_ = bowT_ ? *bowT_ : BOW.draw;
        const double t = s.attack, r = BOW_RELEASE_AT;
        bowT_ = t < r ? *bowShotFrom_ + (BOW.release - *bowShotFrom_) * (t / r) : BOW.release + (BOW.after - BOW.release) * ((t - r) / (1 - r));
        bowShot = true;
      } else {
        if (bowShotFrom_) {
          bowShotFrom_.reset();
          bowT_ = BOW.redraw;
        }
        if (!bowT_ || *bowT_ < BOW.draw) bowT_ = BOW.draw;
        bowT_ = *bowT_ < BOW.full ? std::min(BOW.full, *bowT_ + dt * 1.6) : std::min(BOW.hold, *bowT_ + dt * 0.5);
      }
    } else {
      bowT_.reset();
      bowShotFrom_.reset();
    }
    const double full = bowW_ * (1 - walkW_);
    if (full > 0.001)
      for (const char* k : {"idle", "walk", "run", "guard", "crouchIdle", "crouchWalk", "walkBack", "strafeLeft", "strafeRight"})
        if (Action* a = A(k)) a->weight *= (1 - full);
    bowAct->weight = full;
    if (bowT_) bowAct->time = *bowT_;
  }

  // camadas do lote de combate: seguem o estado do jogo
  const bool busy = s.attack >= 0 || dodging || down || s.mounted || s.air || s.metamorph || s.craft || s.channel;
  const bool crouchOn = s.crouch;
  if (prevCrouch_ && crouchOn != *prevCrouch_ && walkW_ < 0.3 && !busy) {
    const int a = act(crouchOn ? "standToCrouch" : "crouchToStand");
    posture_ = a >= 0 ? std::optional<Posture>(Posture{a, 0}) : std::nullopt;
  }
  prevCrouch_ = crouchOn;
  if (posture_) {
    const double d = duration(posture_->act);
    posture_->t += dt * POSTURE_RATE;
    if (busy || posture_->t >= d) {
      posture_.reset();
    } else {
      mixer_.action(posture_->act).time = posture_->t;
      overlay(posture_->act, std::min(1.0, (d - posture_->t) / (0.15 * POSTURE_RATE)) * (1 - walkW_));
    }
  }
  // impacto pelo lado do golpe: parado, o corpo inteiro; andando, tronco e braços
  if (s.hit && !busy && bowW_ < 0.3) {
    if (const auto c = rpg::anim::hitClip(s.hit->side, hasClip)) {
      const std::string key(rpg::anim::clipKey(*c));
      const int a = act(key);
      const double d = duration(a), t = s.hit->t * HIT_RATE;
      if (t < d) {
        mixer_.action(a).time = t;
        const double env = std::min(1.0, s.hit->t / 0.06) * clamp((d - t) / (0.3 * d), 0, 1);
        overlay(a, env * (1 - walkW_));
        if (walkW_ > 0.01) hitLayer_ = HitLayer{key, t, env * walkW_ * 0.85};
      }
    }
  }
  // fim da esquiva direcional: o clipe termina a recuperação enquanto a locomoção volta
  if (s.dodge < 0 && dodgeTail_) {
    DodgeTail& tl = *dodgeTail_;
    tl.t += dt;
    tl.age += dt;
    const double w = busy ? 0 : (1 - tl.age / DODGE_TAIL) * (1 - walkW_);
    if (w <= 0 || tl.t >= duration(tl.act)) {
      dodgeTail_.reset();
    } else {
      mixer_.action(tl.act).time = tl.t;
      overlay(tl.act, w);
    }
  }

  // ataques e técnicas por clipe
  int activeCombat = -1;
  if (s.attack >= 0 && !bowShot) {
    const double t = s.attack;
    switch (s.kind) {
      case ActionKind::Slash:
      case ActionKind::Spin: activeCombat = act("slash"); break;
      case ActionKind::Thrust: activeCombat = act("thrust"); break;
      case ActionKind::Rapid: activeCombat = act("punch"); break;
      case ActionKind::Heavy: activeCombat = act("smash"); break;
      case ActionKind::Bow:
      case ActionKind::Crossbow:
      case ActionKind::Charge: activeCombat = act("bow"); break;
      case ActionKind::Cast: activeCombat = act("cast"); break;
      default: break;
    }
    if (activeCombat >= 0) {
      const double env = t < 0.12 ? (t / 0.12) : (t > 0.86 ? std::max(0.0, (1 - t) / 0.14) : 1);
      weight(activeCombat) = 0.96 * env;
      mixer_.action(activeCombat).time = clamp(t, 0, 1) * duration(activeCombat);
    }
  }

  const int dodgeAct = dodging && !s.dodgeKey.empty() && s.dodgeKey != "roll" ? act(s.dodgeKey) : -1;
  if (deathAct >= 0) {
    activeCombat = deathAct;
    mixer_.action(deathAct).time = std::min(duration(deathAct) - 1e-3, s.death->t * DEATH_RATE);
    overlay(deathAct, 1);
  } else if (dodgeAct >= 0) {
    activeCombat = dodgeAct;
    std::array<double, 2> m{0, duration(dodgeAct)};
    if (const auto mm = meta_.find(s.dodgeKey); mm != meta_.end() && mm->second.motion) m = *mm->second.motion;
    const double a = std::max(0.0, m[0] - 0.12), p = clamp(s.dodge, 0, 1);
    mixer_.action(dodgeAct).time = p <= DODGE_MOVE ? a + (m[1] - a) * p / DODGE_MOVE : m[1] + (p - DODGE_MOVE) * 0.42;
    overlay(dodgeAct, 1);
    dodgeTail_ = DodgeTail{dodgeAct, mixer_.action(dodgeAct).time, 0};
  } else if (dodging && has("roll")) {
    activeCombat = act("roll");
    weight(activeCombat) = 1;
    mixer_.action(activeCombat).time = clamp(s.dodge, 0, 1) * duration(activeCombat);
  } else if (down && has("down")) {
    activeCombat = act("down");
    weight(activeCombat) = 1;
    mixer_.action(activeCombat).time = std::min(duration(activeCombat), 1.4);
  } else if (s.metamorph && has("roar")) {
    weight(act("roar")) = 0.85;
  } else if (s.craft && has("craft")) {
    weight(act("craft")) = 1;
  }
  for (const char* k : CLIPS_TO_ZERO) {
    const int a = act(k);
    if (a < 0) continue;
    const std::string_view key(k);
    if (a != activeCombat && !(key == "roar" && s.metamorph) && !(key == "craft" && s.craft)) weight(a) = 0;
  }

  // mixerStep: a pose sai do repouso + clipes (a pose procedural do quadro anterior não acumula)
  pose_ = rig_.rest;
  mixer_.update(dt, pose_);

  // andando com o arco puxado: tronco e braços do clipe por cima da caminhada
  const double upperW = bowW_ * walkW_;
  if (upperW > 0.001 && bowT_) {
    upperLayer("bowAim", *bowT_, upperW);
    const double tw = BOW_TWIST * upperW;
    bend("Spine", 0, 1, 0, tw * 0.35);
    bend("Spine1", 0, 1, 0, tw * 0.35);
    bend("Spine2", 0, 1, 0, tw * 0.3);
  }
  if (hitLayer_) {
    upperLayer(hitLayer_->key, hitLayer_->t, hitLayer_->w);
    hitLayer_.reset();
  }

  bodyRot_ = glm::vec3(0.0f);
  bodyPos_ = glm::vec3(0.0f);
  const double w = walkW_, rk = runK * w;
  const double walkK = (1 - runK) * w;
  if (!has("run") && walkK > 0.05) {
    const auto k = [&](double g) { return 1 + (stride - 1) * g * walkK; };
    ampBone("LeftUpLeg", k(1));
    ampBone("RightUpLeg", k(1));
    ampBone("LeftLeg", k(0.8));
    ampBone("RightLeg", k(0.8));
    ampBone("LeftArm", k(0.75));
    ampBone("RightArm", k(0.75));
    ampBone("LeftForeArm", k(0.5));
    ampBone("RightForeArm", k(0.5));
  }
  // a mão da arma fica firme para a arma não chicotear
  if (w > 0.01 && !family_.empty() && family_ != "punhos") {
    const std::string arm = (family_ == "arco" || family_.rfind("tomo_", 0) == 0) ? "Left" : "Right";
    ampBone(arm + "Arm", 0.55);
    ampBone(arm + "ForeArm", 0.5);
    bend(arm + "ForeArm", 1, 0, 0, -0.45 * w);
  }
  if (rk > 0.01) {
    bend("Spine", 1, 0, 0, 0.14 * rk);
    bend("Spine1", 1, 0, 0, 0.06 * rk);
    bend("Neck", 1, 0, 0, -0.10 * rk);
    bend("Head", 1, 0, 0, -0.08 * rk);
    if (!has("run")) {
      bend("LeftForeArm", 1, 0, 0, -0.85 * rk);
      bend("RightForeArm", 1, 0, 0, -0.85 * rk);
      bend("LeftArm", 0, 0, 1, 0.12 * rk);
      bend("RightArm", 0, 0, 1, -0.12 * rk);
      if (const int hips = rig_.bone("Hips"); hips >= 0) {
        float& y = pose_.t[static_cast<std::size_t>(hips)].y;
        y = static_cast<float>(rig_.hipsBase + (static_cast<double>(y) - rig_.hipsBase) * (1 + 0.55 * rk));
      }
    }
  }
  // aceleração e frenagem
  if (smoothAccel_ > 0.5) {
    const double accelPitch = clamp(smoothAccel_ * 0.012, 0, 0.13) * (fwd_ >= 0 ? 1 : -0.7);
    bend("Spine", 1, 0, 0, accelPitch);
    bend("Spine1", 1, 0, 0, accelPitch * 0.5);
  } else if (smoothAccel_ < -2.0) {
    const double brakeWeight = clamp(-smoothAccel_ * 0.012, 0, 0.14);
    bend("Spine", 1, 0, 0, -brakeWeight * 0.6);
    if (const int hips = rig_.bone("Hips"); hips >= 0) pose_.t[static_cast<std::size_t>(hips)].y -= static_cast<float>(brakeWeight * 0.08);
  }
  // equilíbrio lateral no passo de lado (sem os clipes laterais)
  if (std::abs(strafe_) > 0.05 && moving && !(has("strafeLeft") && has("strafeRight"))) {
    const double st = clamp(strafe_, -1, 1);
    bend("Hips", 0, 0, 1, st * 0.04);
    bend("Spine", 0, 0, 1, -st * 0.07);
    bend("Spine1", 0, 0, 1, -st * 0.04);
  }
  // idle vivo: respiração, peso entre os pés e olhares
  if (idleW > 0.05 && !down && s.dodge < 0) {
    const double br = std::sin(phase_ * 0.38), brChest = std::cos(phase_ * 0.38);
    bend("Spine", 1, 0, 0, br * 0.016 * idleW);
    bend("Spine1", 1, 0, 0, brChest * 0.024 * idleW);
    bend("Spine2", 1, 0, 0, brChest * 0.016 * idleW);
    bend("LeftArm", 0, 0, 1, brChest * 0.018 * idleW);
    bend("RightArm", 0, 0, 1, -brChest * 0.018 * idleW);
    const double sway = std::sin(phase_ * 0.14);
    bend("Hips", 0, 0, 1, sway * 0.022 * idleW);
    bend("Spine", 0, 0, 1, -sway * 0.028 * idleW);
    bend("LeftUpLeg", 0, 0, 1, -sway * 0.015 * idleW);
    bend("RightUpLeg", 0, 0, 1, -sway * 0.015 * idleW);
    glanceTimer_ -= dt;
    if (glanceTimer_ <= 0) {
      glanceTimer_ = 2.2 + rand() * 3.2;
      targetGlanceX_ = (rand() - 0.5) * 0.22;
      targetGlanceY_ = (rand() - 0.5) * 0.12;
    }
    lookX_ = approach(lookX_, s.aim > 0 ? 0 : targetGlanceX_, dt * 3.5);
    lookY_ = approach(lookY_, s.aim > 0 ? 0 : targetGlanceY_, dt * 3.5);
    if (!(s.aim > 0) && idleW > 0.4) {
      bend("Neck", 0, 1, 0, lookX_ * idleW);
      bend("Head", 0, 1, 0, lookX_ * 0.65 * idleW);
      bend("Head", 1, 0, 0, lookY_ * idleW);
    }
  }
  // mirando: ombros de lado, arma na linha do olhar, cabeça no alvo
  const double aw = s.aim, bw = bowW_;
  if (bw > 0.01 && !down && s.dodge < 0) {
    const double ap = s.aimPitch;
    if (std::abs(ap) > 0.001) {
      bend("Spine", 1, 0, 0, -ap * 0.35 * bw);
      bend("Spine1", 1, 0, 0, -ap * 0.25 * bw);
      bend("Neck", 1, 0, 0, -ap * 0.18 * bw);
      bend("Head", 1, 0, 0, -ap * 0.12 * bw);
    }
  } else if (aw > 0.01 && !down && s.dodge < 0 && !(s.attack >= 0)) {
    const bool leftLead = family_ == "arco" || family_.rfind("tomo_", 0) == 0;
    const std::string lead = leftLead ? "Left" : "Right", off = leftLead ? "Right" : "Left";
    const double sg = leftLead ? -1 : 1;
    const bool tiro = family_ == "arco" || family_ == "besta" || family_.rfind("tomo_", 0) == 0 || family_.rfind("cajado_", 0) == 0;
    for (const std::string& k : {lead + "Arm", lead + "ForeArm", off + "Arm", off + "ForeArm"}) ampBone(k, 1 - aw);
    bend("Spine", 0, 1, 0, sg * 0.10 * aw);
    bend("Spine1", 0, 1, 0, sg * 0.12 * aw);
    bend("Neck", 0, 1, 0, -sg * 0.16 * aw);
    bend("Head", 1, 0, 0, -0.05 * aw);
    bend(lead + "Arm", 0, 0, 1, sg * 0.10 * aw);
    bend(lead + "Arm", 1, 0, 0, (tiro ? -1.00 : -0.62) * aw);
    bend(lead + "ForeArm", 1, 0, 0, (tiro ? 0.27 : -0.34) * aw);
    bend(off + "Arm", 0, 0, 1, sg * 0.18 * aw);
    bend(off + "Arm", 1, 0, 0, (tiro ? -0.85 : -0.55) * aw);
    bend(off + "ForeArm", 1, 0, 0, (tiro ? -1.45 : -0.95) * aw);
    const double ap = s.aimPitch;
    if (std::abs(ap) > 0.001) {
      bend("Spine", 1, 0, 0, -ap * 0.35 * aw);
      bend("Spine1", 1, 0, 0, -ap * 0.25 * aw);
      bend("Neck", 1, 0, 0, -ap * 0.18 * aw);
      bend("Head", 1, 0, 0, -ap * 0.12 * aw);
      bend(lead + "Arm", 1, 0, 0, -ap * 0.45 * aw);
      bend(off + "Arm", 1, 0, 0, -ap * 0.35 * aw);
    }
  }
  if (s.metamorph) {
    bend("Spine", 1, 0, 0, 0.38);
    bend("Spine1", 1, 0, 0, 0.22);
    bend("Neck", 1, 0, 0, -0.32);
    bend("LeftArm", 1, 0, 0, -0.85);
    bend("RightArm", 1, 0, 0, -0.85);
    bend("LeftForeArm", 1, 0, 0, -0.8);
    bend("RightForeArm", 1, 0, 0, -0.8);
    bend("LeftArm", 0, 0, 1, 0.3);
    bend("RightArm", 0, 0, 1, -0.3);
  }
  if (down) {
    if (deathAct >= 0 || has("down")) {
      postura(dt, 0, world);
      return true;
    }
    bodyRot_.x = static_cast<float>(-kPi / 2 + 0.08);
    bodyPos_ = glm::vec3(0.0f, 0.16f, -0.2f);
    bend("LeftArm", 0, 0, 1, 0.6);
    bend("RightArm", 0, 0, 1, -0.5);
    return true;
  }
  if (s.mounted) {
    for (const auto& [k, side] : {std::pair{"LeftUpLeg", 1.0}, std::pair{"RightUpLeg", -1.0}}) {
      bend(k, 0, 0, 1, side * 0.42);
      bend(k, 1, 0, 0, -1.35);
    }
    bend("LeftLeg", 1, 0, 0, 1.45);
    bend("RightLeg", 1, 0, 0, 1.45);
    bend("LeftArm", 1, 0, 0, -0.55);
    bend("RightArm", 1, 0, 0, -0.55);
    bend("LeftForeArm", 1, 0, 0, -0.7);
    bend("RightForeArm", 1, 0, 0, -0.7);
    bodyPos_.y = static_cast<float>(std::sin(phase_ * 2) * 0.03 * std::min(1.0, mps));
    return true;
  }
  if (dodging) {
    if (dodgeAct >= 0 || has("roll")) {
      postura(dt, w * 0.5, world);
      return true;
    }
    // rolamento procedural em torno do quadril (sem o clipe)
    const double th = s.dodge * kPi * 2, hy = hipsY_, cy = hy - 0.45 * std::sin(s.dodge * kPi);
    bodyRot_.x = static_cast<float>(th);
    bodyPos_ = glm::vec3(0.0f, static_cast<float>(cy - hy * std::cos(th)), static_cast<float>(-hy * std::sin(th)));
    bend("Spine", 1, 0, 0, 0.5);
    bend("LeftUpLeg", 1, 0, 0, -1.7);
    bend("RightUpLeg", 1, 0, 0, -1.7);
    bend("LeftLeg", 1, 0, 0, 2.0);
    bend("RightLeg", 1, 0, 0, 2.0);
    return true;
  }
  if (s.channel) {
    bend("Spine", 1, 0, 0, 0.3);
    bend("RightArm", 1, 0, 0, -1.25 + std::sin(phase_ * 1.8) * 0.7);
    bend("RightForeArm", 1, 0, 0, -0.4);
    bend("LeftArm", 1, 0, 0, -0.7);
  }
  if (s.attack >= 0 && !bowShot) {
    const double t = s.attack;
    if (activeCombat >= 0) {
      // clipe de mocap: impulso no tronco e mira vertical
      const double impactT = std::sin(clamp((t - 0.2) / 0.5, 0, 1) * kPi);
      bend("Spine", 1, 0, 0, 0.09 * impactT);
      const double ap = s.aimPitch;
      if (std::abs(ap) > 0.001) {
        bend("Spine", 1, 0, 0, -ap * 0.28);
        bend("Spine1", 1, 0, 0, -ap * 0.20);
        bend("Head", 1, 0, 0, -ap * 0.15);
      }
    } else if (s.kind == ActionKind::Bow || s.kind == ActionKind::Charge) {
      const double pull = s.kind == ActionKind::Charge ? std::min(1.0, t * 1.2) : std::min(1.0, t * 2);
      bend("Spine", 0, 1, 0, -0.35);
      bend("LeftArm", 1, 0, 0, -1.5);
      bend("LeftArm", 0, 1, 0, 0.35);
      bend("RightArm", 1, 0, 0, -1.45);
      bend("RightArm", 0, 1, 0, 0.2 - pull * 0.5);
      bend("RightForeArm", 0, 1, 0, -0.4 - pull * 1.7);
    } else if (s.kind == ActionKind::Crossbow) {
      bend("Spine", 0, 1, 0, -0.2);
      bend("RightArm", 1, 0, 0, -1.45);
      bend("LeftArm", 1, 0, 0, -1.35);
      bend("LeftForeArm", 1, 0, 0, -0.65);
    } else if (s.kind == ActionKind::Spin) {
      bodyRot_.y = static_cast<float>(t * kPi * 2);
      bend("RightArm", 0, 0, 1, -1.25);
      bend("RightArm", 1, 0, 0, -0.5);
      bend("LeftArm", 0, 0, 1, 1.2);
    } else if (s.kind == ActionKind::Thrust) {
      bend("Spine", 1, 0, 0, 0.3);
      bend("RightArm", 1, 0, 0, -1.5);
      bend("RightForeArm", 1, 0, 0, 0.15);
      bend("LeftUpLeg", 1, 0, 0, -0.7);
      bend("RightUpLeg", 1, 0, 0, 0.55);
      bend("LeftLeg", 1, 0, 0, 0.6);
    } else if (s.kind == ActionKind::Rapid) {
      const double punch = std::sin(t * kPi * 4);
      bend("RightArm", 1, 0, 0, -1.35 - punch * 0.45);
      bend("LeftArm", 1, 0, 0, -1.35 + punch * 0.45);
      bend("Spine", 0, 1, 0, punch * 0.22);
    } else if (s.kind == ActionKind::Heavy) {
      const double up = t < 0.4 ? t / 0.4 : 1 - (t - 0.4) / 0.6;
      bend("RightArm", 1, 0, 0, -2.8 * up + 0.3 * (1 - up));
      bend("LeftArm", 1, 0, 0, -2.4 * up + 0.2 * (1 - up));
      bend("Spine", 1, 0, 0, -0.15 * up + 0.35 * (1 - up));
    } else if (s.kind == ActionKind::Cast) {
      const double wave = std::sin(t * kPi);
      bend("Spine", 1, 0, 0, -0.15 * wave);
      bend("RightArm", 1, 0, 0, -1.6 - wave * 0.35);
      bend("LeftArm", 1, 0, 0, -1.2 - wave * 0.25);
      bend("RightForeArm", 0, 1, 0, 0.45 * wave);
    } else if (s.kind == ActionKind::Slash) {
      bodyRot_.y = static_cast<float>((t - 0.5) * 1.6);
      bend("RightArm", 0, 0, 1, -1.1);
      bend("RightArm", 1, 0, 0, -0.7 + t * 0.9);
      bend("LeftArm", 0, 0, 1, 0.85);
    } else {
      // golpe descendente padrão
      const double up = t < 0.35 ? t / 0.35 : 1 - (t - 0.35) / 0.65;
      const double a = t < 0.35 ? -2.7 * up : -2.7 + ((t - 0.35) / 0.65) * 2.4;
      bend("Spine", 0, 1, 0, t < 0.35 ? 0.35 * up : 0.35 - (t - 0.35) * 1.2);
      bend("Spine", 1, 0, 0, t < 0.35 ? -0.08 * up : 0.25 * (1 - up));
      bend("RightArm", 1, 0, 0, a);
      bend("RightArm", 0, 0, 1, -0.25);
      bend("RightForeArm", 1, 0, 0, -0.5 * up);
      bend("LeftArm", 1, 0, 0, -0.45);
    }
  }
  postura(dt, w, world);
  return true;
}

}  // namespace rpg::client::anim
