#include "client/net/Predictor.h"

#include <algorithm>
#include <cmath>

#include "core/JsMath.h"
#include "core/movement/MoveEntity.h"
#include "core/protocol/Replicated.h"

namespace rpg::client {

namespace proto = protocol;

void Predictor::step(const GameData& data, const StaticMap& map, State& s, const Inputs& in, const proto::PlayerCommand& cmd,
                     const proto::PlayerCommand& prev, double dt) {
  const auto& M = data.balance.movement;
  const bool canAct = !cmd.has(proto::kHeldUiOpen) && !in.cinematic;
  const auto hit = [&](proto::Press p) { return cmd.count(p) != prev.count(p); };
  s.metamorphT = std::max(0.0, s.metamorphT - dt);

  double mx = 0, mz = 0;
  if (canAct) {
    const double cy = cmd.camYaw;
    const double f = std::clamp<int>(cmd.moveForward, -1, 1);
    const double r = std::clamp<int>(cmd.moveRight, -1, 1);
    mx = -js::sin(cy) * f + js::cos(cy) * r;
    mz = -js::cos(cy) * f - js::sin(cy) * r;
    const double l = jsHypot(mx, mz);
    if (l > 0) {
      mx /= l;
      mz /= l;
    }
  }
  const bool moving = mx != 0 || mz != 0;
  const bool shift = cmd.has(proto::kHeldRun);
  const bool aiming = canAct && !in.mounted && cmd.has(proto::kHeldAim);
  const bool crouch = canAct && !in.mounted && !s.air && cmd.has(proto::kHeldCrouch);
  const motor::Wading wade = motor::wadingAt(map, s.pos.x, s.pos.z);
  motor::SpeedInputs si;
  si.running = !aiming && !crouch && shift;
  si.crouch = crouch;
  si.aiming = aiming;
  si.metamorph = s.metamorphT > 0;
  si.mounted = in.mounted;
  si.wading = wade.wading;
  si.load = in.load;
  si.armorSpeed = in.armorSpeed;
  const double sp = motor::moveSpeed(M, si, s.landT, dt);

  if (s.air) {
    motor::airSteer(M, map, *s.air, s.pos, moving, mx, mz, sp, dt);
  } else if (moving) {
    moveEntity(map, s.pos, mx * sp * dt, mz * sp * dt, in.mounted ? motor::kMountedRadius : motor::kBodyRadius, 0.0);
  }
  // updatePlayer: no chão, sem disparo mirado, o Espaço pula (com carga normal e fôlego)
  if (canAct && !in.mounted && !s.air && !(aiming && hit(proto::Press::Fire)) && hit(proto::Press::Jump)) {
    const double cost = shift ? M.boost.vigor : M.jump.vigor;
    if (in.load == motor::LoadState::Normal && s.vigor >= cost) {
      s.vigor -= cost;
      s.air = motor::startJump(M, shift, moving ? mx : 0, moving ? mz : 0, sp, s.yaw, in.armorSpeed);
    }
  }
  motor::applyGravity(M, s.air, s.landT, dt);
  s.y = motor::bodyHeight(map, s.pos.x, s.pos.z, s.air, wade);
}

void Predictor::sent(const proto::PlayerCommand& cmd, double dt) {
  unacked_.push_back({cmd, dt});
  while (unacked_.size() > 64) unacked_.pop_front();  // sem resposta por 2 s: não cresce sem fim
  // o passo previsto já anda com o comando enviado (o snapshot que o confirma vem depois)
  if (active_ && have_ && map_) {
    prevPos_ = glm::dvec3(state_.pos.x, state_.y, state_.pos.z);
    step(data_, *map_, state_, inputs_, cmd, lastSent_, dt);
  }
  lastSent_ = cmd;
}

glm::dvec3 Predictor::position(double alpha) const {
  const glm::dvec3 cur(state_.pos.x, state_.y, state_.pos.z);
  return glm::mix(prevPos_, cur, std::clamp(alpha, 0.0, 1.0)) + offset_;
}

void Predictor::reset() {
  unacked_.clear();
  active_ = have_ = false;
  offset_ = glm::dvec3(0.0);
  ackedCmd_ = {};
}

void Predictor::reconcile(const proto::EntityState& me, const proto::PrivateState& self, const StaticMap& map) {
  const glm::dvec3 shown = position();
  map_ = &map;
  while (!unacked_.empty() && unacked_.front().cmd.seq <= self.lastCommandSeq) {
    ackedCmd_ = unacked_.front().cmd;
    unacked_.pop_front();
  }
  const bool free = static_cast<proto::PlayerState>(self.state) == proto::PlayerState::Free;
  active_ = free && !self.cinematic && self.attackTarget == 0 && self.lastCommandSeq != 0;
  if (!active_) {
    have_ = false;
    offset_ = glm::dvec3(0.0);
    return;
  }
  // estado do servidor no passo do comando confirmado
  state_.pos = {me.x, me.z};
  state_.y = me.y;
  state_.yaw = me.yaw;
  state_.landT = self.landT;
  state_.vigor = self.vigor;
  state_.metamorphT = self.metamorphT;
  state_.air.reset();
  if (self.airY) state_.air = motor::Air{*self.airY, self.airVy, self.airVx, self.airVz, self.airBoosted, self.airT, self.airDur};
  inputs_.load = static_cast<motor::LoadState>(self.load);
  inputs_.mounted = self.mounted;
  inputs_.cinematic = self.cinematic;
  inputs_.armorSpeed =
      self.armor && data_.items.contains(ItemId{self.armor->item}) ? data_.items[ItemId{self.armor->item}].armorSpeed : 1.0;
  // refaz o que o servidor ainda não aplicou
  proto::PlayerCommand prev = ackedCmd_;
  for (const Sent& s : unacked_) {
    step(data_, map, state_, inputs_, s.cmd, prev, s.dt);
    prev = s.cmd;
  }
  const glm::dvec3 predicted(state_.pos.x, state_.y, state_.pos.z);
  prevPos_ = predicted;
  if (!have_) {
    offset_ = glm::dvec3(0.0);
  } else {
    offset_ = shown - predicted;
    lastError_ = glm::length(offset_);
    if (lastError_ > 4.0) offset_ = glm::dvec3(0.0);  // teleporte, reaparecer, troca de mapa
  }
  have_ = true;
}

void Predictor::update(double frameDt) { offset_ *= std::exp(-frameDt * 12.0); }

}  // namespace rpg::client
