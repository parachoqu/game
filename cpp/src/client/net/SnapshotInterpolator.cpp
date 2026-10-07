#include "client/net/SnapshotInterpolator.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "core/Math.h"

namespace rpg::client {

namespace {

double serverTime(const protocol::Snapshot& s, double rate) { return static_cast<double>(s.world.tick) / rate; }

}  // namespace

void SnapshotInterpolator::clear() {
  buffer_.clear();
  offset_.reset();
}

void SnapshotInterpolator::push(protocol::Snapshot snap, double localTime) {
  if (!buffer_.empty() && snap.world.tick < buffer_.back().world.tick) {
    ++dropped_;
    return;
  }
  const double o = serverTime(snap, tickRate_) - localTime;
  if (!offset_ || std::abs(o - *offset_) > 1.0) offset_ = o;  // primeiro snapshot ou salto (pausa, reconexão)
  else if (o > *offset_) *offset_ += (o - *offset_) * 0.25;   // chegou adiantado: alcança depressa
  else *offset_ += (o - *offset_) * 0.02;                     // atrasado: pode ser só variação da rede
  if (!buffer_.empty() && snap.world.tick == buffer_.back().world.tick) buffer_.back() = std::move(snap);  // pausado
  else buffer_.push_back(std::move(snap));
  // guarda ~1 s de histórico
  const std::size_t keep = static_cast<std::size_t>(std::max(8.0, tickRate_));
  while (buffer_.size() > keep) buffer_.pop_front();
}

double SnapshotInterpolator::renderServerTime(double localTime) const {
  if (!offset_) return 0;
  return localTime + *offset_ - delayTicks_ / tickRate_;
}

protocol::EntityState lerpEntity(const protocol::EntityState& a, const protocol::EntityState& b, double t) {
  protocol::EntityState r = b;
  if (a.map != b.map || a.instance != b.instance) return r;
  // teleporte (reaparecer, entrar no portal, tp de depuração): nada de deslizar pelo mapa
  const double jump = std::hypot(b.x - a.x, b.z - a.z);
  if (jump > 12) return r;
  r.x = lerp(a.x, b.x, t);
  r.y = lerp(a.y, b.y, t);
  r.z = lerp(a.z, b.z, t);
  r.yaw = lerpAngle(a.yaw, b.yaw, t);
  // a direção da esquiva só existe enquanto ela dura: no primeiro snapshot dela, a anterior vale 0 e
  // misturar daria um meio-termo (o clipe e o giro do corpo são escolhidos nesse quadro)
  if (a.dodgeProgress >= 0 && b.dodgeProgress >= 0) r.dodgeYaw = lerpAngle(a.dodgeYaw, b.dodgeYaw, t);
  r.speed = lerp(a.speed, b.speed, t);
  r.aimPitch = lerp(a.aimPitch, b.aimPitch, t);
  // direção do passo: a demo usa o vetor unitário (mx, mz); a mistura de dois encurta nas viradas
  r.moveX = lerp(a.moveX, b.moveX, t);
  r.moveZ = lerp(a.moveZ, b.moveZ, t);
  if (const double len = std::hypot(r.moveX, r.moveZ); (b.flags & protocol::kFlagMoving) != 0 && len > 1e-6) {
    r.moveX /= len;
    r.moveZ /= len;
  }
  if (a.action == b.action && a.actionProgress >= 0 && b.actionProgress >= a.actionProgress)
    r.actionProgress = lerp(a.actionProgress, b.actionProgress, t);
  if (a.dodgeProgress >= 0 && b.dodgeProgress >= a.dodgeProgress) r.dodgeProgress = lerp(a.dodgeProgress, b.dodgeProgress, t);
  if (a.airPhase >= 0 && b.airPhase >= a.airPhase) r.airPhase = lerp(a.airPhase, b.airPhase, t);
  if (b.stateT >= a.stateT) r.stateT = lerp(a.stateT, b.stateT, t);
  if (b.deadT >= a.deadT) r.deadT = lerp(a.deadT, b.deadT, t);
  // G.time - hitAt corre a cada quadro na demo; um golpe novo (b < a) vale direto
  if (a.hitAge >= 0 && b.hitAge >= a.hitAge) r.hitAge = lerp(a.hitAge, b.hitAge, t);
  r.hpFrac = lerp(a.hpFrac, b.hpFrac, t);
  return r;
}

std::vector<protocol::EntityState> SnapshotInterpolator::sample(double localTime) {
  if (buffer_.empty()) return {};
  const double rt = renderServerTime(localTime);
  // primeiro snapshot depois do instante desenhado
  std::size_t hi = 0;
  while (hi < buffer_.size() && serverTime(buffer_[hi], tickRate_) < rt) ++hi;
  if (hi == 0) return buffer_.front().entities;
  if (hi >= buffer_.size()) return buffer_.back().entities;  // sem snapshot novo: segura o último
  const protocol::Snapshot& A = buffer_[hi - 1];
  const protocol::Snapshot& B = buffer_[hi];
  const double ta = serverTime(A, tickRate_), tb = serverTime(B, tickRate_);
  const double t = tb > ta ? clamp((rt - ta) / (tb - ta), 0.0, 1.0) : 1.0;
  std::unordered_map<std::uint32_t, const protocol::EntityState*> prev;
  prev.reserve(A.entities.size());
  for (const auto& e : A.entities) prev.emplace(e.id, &e);
  std::vector<protocol::EntityState> out;
  out.reserve(B.entities.size());
  for (const auto& e : B.entities) {
    const auto it = prev.find(e.id);
    out.push_back(it == prev.end() ? e : lerpEntity(*it->second, e, t));
  }
  return out;
}

}  // namespace rpg::client
