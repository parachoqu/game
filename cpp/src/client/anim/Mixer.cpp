#include "client/anim/Mixer.h"

#include <cmath>

namespace rpg::client::anim {

namespace {

void slerpInPlace(double* dst, const double* src, double t) {
  const glm::quat a(static_cast<float>(dst[3]), static_cast<float>(dst[0]), static_cast<float>(dst[1]), static_cast<float>(dst[2]));
  const glm::quat b(static_cast<float>(src[3]), static_cast<float>(src[0]), static_cast<float>(src[1]), static_cast<float>(src[2]));
  const glm::quat r = slerpThree(a, b, t);
  dst[0] = static_cast<double>(r.x);
  dst[1] = static_cast<double>(r.y);
  dst[2] = static_cast<double>(r.z);
  dst[3] = static_cast<double>(r.w);
}

}  // namespace

Mixer::Mixer(const Rig& rig) : rig_(rig) {}

int Mixer::binding(int node, Track::Path path) {
  for (std::size_t i = 0; i < bindings_.size(); ++i)
    if (bindings_[i].node == node && bindings_[i].path == path) return static_cast<int>(i);
  Binding b;
  b.node = node;
  b.path = path;
  const auto n = static_cast<std::size_t>(node);
  if (path == Track::Path::Quaternion) {
    const glm::quat& q = rig_.rest.r[n];
    b.original[0] = static_cast<double>(q.x);
    b.original[1] = static_cast<double>(q.y);
    b.original[2] = static_cast<double>(q.z);
    b.original[3] = static_cast<double>(q.w);
  } else {
    const glm::vec3& v = path == Track::Path::Position ? rig_.rest.t[n] : rig_.rest.s[n];
    b.original[0] = static_cast<double>(v.x);
    b.original[1] = static_cast<double>(v.y);
    b.original[2] = static_cast<double>(v.z);
  }
  bindings_.push_back(b);
  return static_cast<int>(bindings_.size()) - 1;
}

int Mixer::add(const Clip* clip) {
  Action a;
  a.clip = clip;
  if (clip)
    for (const Track& t : clip->tracks) a.bindings.push_back(binding(t.node, t.path));
  actions_.push_back(std::move(a));
  return static_cast<int>(actions_.size()) - 1;
}

// AnimationAction._updateTime com LoopRepeat e repetições infinitas
double Mixer::updateTime(Action& a, double dt) {
  const double duration = a.clip->duration;
  double time = a.time + dt;
  if (dt == 0) return time;  // tempo posto à mão: não dá a volta
  if (a.loopCount == -1 && dt >= 0) a.loopCount = 0;
  if (duration > 0 && (time >= duration || time < 0)) {
    const double loopDelta = std::floor(time / duration);
    time -= duration * loopDelta;
    a.loopCount += static_cast<int>(std::abs(loopDelta));
  }
  a.time = time;
  return time;
}

void Mixer::update(double dt, Pose& pose) {
  for (Binding& b : bindings_) b.weight = 0;
  float value[4];
  for (Action& a : actions_) {
    if (!a.clip) continue;
    const double clipTime = updateTime(a, dt * a.timeScale);
    if (!(a.weight > 0)) continue;
    for (std::size_t k = 0; k < a.clip->tracks.size(); ++k) {
      const Track& tr = a.clip->tracks[k];
      Binding& b = bindings_[static_cast<std::size_t>(a.bindings[k])];
      sampleTrack(tr, clipTime, value);
      const std::size_t n = tr.path == Track::Path::Quaternion ? 4 : 3;
      double incoming[4] = {static_cast<double>(value[0]), static_cast<double>(value[1]), static_cast<double>(value[2]), n == 4 ? static_cast<double>(value[3]) : 0.0};
      if (b.weight == 0) {
        for (std::size_t c = 0; c < n; ++c) b.accum[c] = incoming[c];
        b.weight = a.weight;
      } else {
        b.weight += a.weight;
        const double mix = a.weight / b.weight;
        if (n == 4) slerpInPlace(b.accum, incoming, mix);
        else
          for (std::size_t c = 0; c < 3; ++c) b.accum[c] = b.accum[c] * (1 - mix) + incoming[c] * mix;
      }
    }
  }
  // PropertyMixer.apply: completa com a pose original o que faltar até 1
  for (Binding& b : bindings_) {
    double out[4] = {b.accum[0], b.accum[1], b.accum[2], b.accum[3]};
    if (b.weight < 1) {
      const double t = 1 - b.weight;
      if (b.path == Track::Path::Quaternion) {
        if (b.weight == 0) {
          for (int c = 0; c < 4; ++c) out[c] = b.original[c];
        } else {
          slerpInPlace(out, b.original, t);
        }
      } else {
        for (int c = 0; c < 3; ++c) out[c] = b.weight == 0 ? b.original[c] : out[c] * (1 - t) + b.original[c] * t;
      }
    }
    const auto n = static_cast<std::size_t>(b.node);
    if (b.path == Track::Path::Quaternion)
      pose.r[n] = glm::quat(static_cast<float>(out[3]), static_cast<float>(out[0]), static_cast<float>(out[1]), static_cast<float>(out[2]));
    else if (b.path == Track::Path::Position)
      pose.t[n] = glm::vec3(static_cast<float>(out[0]), static_cast<float>(out[1]), static_cast<float>(out[2]));
    else
      pose.s[n] = glm::vec3(static_cast<float>(out[0]), static_cast<float>(out[1]), static_cast<float>(out[2]));
  }
}

}  // namespace rpg::client::anim
