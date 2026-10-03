#include "client/anim/Rig.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace rpg::client::anim {

namespace {

glm::quat fromThree(const float* v) { return glm::quat(v[3], v[0], v[1], v[2]); }

// slerpFlat em double, sobre (x, y, z, w)
void slerpFlat(double* dst, const double* a, const double* b, double t) {
  double x0 = a[0], y0 = a[1], z0 = a[2], w0 = a[3];
  double x1 = b[0], y1 = b[1], z1 = b[2], w1 = b[3];
  if (w0 != w1 || x0 != x1 || y0 != y1 || z0 != z1) {
    double dot = x0 * x1 + y0 * y1 + z0 * z1 + w0 * w1;
    if (dot < 0) {
      x1 = -x1;
      y1 = -y1;
      z1 = -z1;
      w1 = -w1;
      dot = -dot;
    }
    double s = 1 - t;
    if (dot < 0.9995) {
      const double theta = std::acos(dot), sn = std::sin(theta);
      s = std::sin(s * theta) / sn;
      t = std::sin(t * theta) / sn;
      x0 = x0 * s + x1 * t;
      y0 = y0 * s + y1 * t;
      z0 = z0 * s + z1 * t;
      w0 = w0 * s + w1 * t;
    } else {
      x0 = x0 * s + x1 * t;
      y0 = y0 * s + y1 * t;
      z0 = z0 * s + z1 * t;
      w0 = w0 * s + w1 * t;
      const double f = 1 / std::sqrt(x0 * x0 + y0 * y0 + z0 * z0 + w0 * w0);
      x0 *= f;
      y0 *= f;
      z0 *= f;
      w0 *= f;
    }
  }
  dst[0] = x0;
  dst[1] = y0;
  dst[2] = z0;
  dst[3] = w0;
}

}  // namespace

glm::quat slerpThree(const glm::quat& a, const glm::quat& b, double t) {
  const double qa[4] = {static_cast<double>(a.x), static_cast<double>(a.y), static_cast<double>(a.z), static_cast<double>(a.w)};
  const double qb[4] = {static_cast<double>(b.x), static_cast<double>(b.y), static_cast<double>(b.z), static_cast<double>(b.w)};
  double r[4];
  slerpFlat(r, qa, qb, t);
  return glm::quat(static_cast<float>(r[3]), static_cast<float>(r[0]), static_cast<float>(r[1]), static_cast<float>(r[2]));
}

void sampleTrack(const Track& tr, double time, float* out) {
  const std::size_t n = tr.times.size(), k = tr.size;
  if (n == 0) return;
  const auto copy = [&](std::size_t i) { std::memcpy(out, &tr.values[i * k], k * sizeof(float)); };
  if (time < static_cast<double>(tr.times[0])) return copy(0);
  if (!(time < static_cast<double>(tr.times[n - 1]))) return copy(n - 1);
  // primeiro quadro estritamente depois de `time`
  const auto it = std::upper_bound(tr.times.begin(), tr.times.end(), static_cast<float>(time));
  std::size_t i1 = static_cast<std::size_t>(it - tr.times.begin());
  while (i1 < n && !(time < static_cast<double>(tr.times[i1]))) ++i1;
  while (i1 > 1 && time < static_cast<double>(tr.times[i1 - 1])) --i1;
  const std::size_t i0 = i1 - 1;
  if (tr.step) return copy(i0);
  const double t0 = static_cast<double>(tr.times[i0]), t1 = static_cast<double>(tr.times[i1]);
  const double alpha = (time - t0) / (t1 - t0);
  if (tr.path == Track::Path::Quaternion) {
    double a[4], b[4], r[4];
    for (std::size_t c = 0; c < 4; ++c) {
      a[c] = static_cast<double>(tr.values[i0 * 4 + c]);
      b[c] = static_cast<double>(tr.values[i1 * 4 + c]);
    }
    slerpFlat(r, a, b, alpha);
    for (std::size_t c = 0; c < 4; ++c) out[c] = static_cast<float>(r[c]);
  } else {
    const double w1 = alpha, w0 = 1 - w1;
    for (std::size_t c = 0; c < k; ++c) out[c] = static_cast<float>(static_cast<double>(tr.values[i0 * k + c]) * w0 + static_cast<double>(tr.values[i1 * k + c]) * w1);
  }
}

std::string_view boneKey(std::string_view name) {
  constexpr std::string_view kPrefix = "mixamorig";
  if (name.substr(0, kPrefix.size()) != kPrefix) return name;
  std::size_t i = kPrefix.size();
  while (i < name.size() && name[i] >= '0' && name[i] <= '9') ++i;
  if (i < name.size() && name[i] == ':') ++i;
  return name.substr(i);
}

glm::mat4 composeTRS(const glm::vec3& t, const glm::quat& r, const glm::vec3& s) {
  glm::mat4 m = glm::mat4_cast(r);
  m[0] *= s.x;
  m[1] *= s.y;
  m[2] *= s.z;
  m[3] = glm::vec4(t, 1.0f);
  return m;
}

int Rig::bone(std::string_view key) const {
  const auto it = bones.find(std::string(key));
  return it == bones.end() ? -1 : it->second;
}

const Clip* Rig::clip(std::string_view key) const {
  const auto it = clips.find(key);
  return it == clips.end() ? nullptr : &it->second;
}

Rig Rig::fromPack(const ScenePack& pack, const std::string& id) {
  const auto pit = pack.rigs.find(id);
  if (pit == pack.rigs.end()) throw std::runtime_error("pacote sem o personagem " + id);
  const PackRig& P = pit->second;
  Rig R;
  R.id = id;
  R.kind = P.kind;
  R.names = P.names;
  R.parent = P.parent;
  R.isBone = P.bone;
  const std::vector<float> rest = pack.floats(P.rest);
  const std::size_t n = P.names.size();
  if (rest.size() < n * 10) throw std::runtime_error("pose de repouso incompleta: " + id);
  R.rest.t.resize(n);
  R.rest.r.resize(n);
  R.rest.s.resize(n);
  for (std::size_t i = 0; i < n; ++i) {
    const float* v = &rest[i * 10];
    R.rest.t[i] = {v[0], v[1], v[2]};
    R.rest.r[i] = fromThree(v + 3);
    R.rest.s[i] = {v[7], v[8], v[9]};
  }
  for (std::size_t i = 0; i < n; ++i)
    if (P.bone[i]) R.bones[std::string(boneKey(P.names[i]))] = static_cast<int>(i);
  for (const PackRigMesh& m : P.meshes) {
    RigMesh rm;
    rm.node = m.node;
    rm.geometry = m.geometry;
    rm.material = m.material;
    rm.skinned = m.skinned;
    if (m.skinned) {
      const std::vector<float> bind = pack.floats(m.bindMatrix), inv = pack.floats(m.boneInverses);
      std::memcpy(&rm.bind[0][0], bind.data(), 64);
      rm.bones = m.bones;
      rm.inverses.resize(m.bones.size());
      for (std::size_t b = 0; b < m.bones.size(); ++b) std::memcpy(&rm.inverses[b][0][0], &inv[b * 16], 64);
    }
    R.meshes.push_back(std::move(rm));
  }
  for (const auto& [key, pc] : P.clips) {
    Clip c;
    c.key = key;
    c.duration = pc.duration;
    for (const PackTrack& pt : pc.tracks) {
      if (pt.node < 0 || pt.path == PackTrack::Path::Other) continue;
      Track t;
      t.node = pt.node;
      t.path = pt.path == PackTrack::Path::Position ? Track::Path::Position : pt.path == PackTrack::Path::Quaternion ? Track::Path::Quaternion : Track::Path::Scale;
      t.step = pt.step;
      t.size = pt.size;
      t.times = pack.floats(pt.times);
      t.values = pack.floats(pt.values);
      c.tracks.push_back(std::move(t));
    }
    R.clips[key] = std::move(c);
  }
  R.clipOrder = P.clipOrder;
  R.hipsY = P.hipsY;
  R.hipsBase = P.hipsBase;
  for (const auto& [k, v] : P.idleRef) R.idleRef[k] = glm::quat(v.w, v.x, v.y, v.z);
  R.cycle = P.cycle;
  const auto grip = [&](const std::optional<PackGrip>& g) -> std::optional<Grip> {
    if (!g) return std::nullopt;
    int node = -1;
    for (std::size_t i = 0; i < n; ++i)
      if (P.names[i] == g->bone) {
        node = static_cast<int>(i);
        break;
      }
    if (node < 0) return std::nullopt;
    return Grip{node, composeTRS(g->t, glm::quat(g->r.w, g->r.x, g->r.y, g->r.z), g->s)};
  };
  R.gripR = grip(P.gripR);
  R.gripL = grip(P.gripL);
  R.seatY = P.seatY;
  R.seatZ = P.seatZ;
  return R;
}

glm::mat4 eulerXYZ(const glm::vec3& r) {
  const glm::quat qx = glm::angleAxis(r.x, glm::vec3(1, 0, 0)), qy = glm::angleAxis(r.y, glm::vec3(0, 1, 0)), qz = glm::angleAxis(r.z, glm::vec3(0, 0, 1));
  return glm::mat4_cast(qx * qy * qz);
}

void bend(const Rig& rig, Pose& pose, std::string_view key, double x, double y, double z, double angle) {
  const int b = rig.bone(key);
  if (b < 0 || angle == 0) return;
  glm::quat chain(1, 0, 0, 0);
  for (int o = rig.parent[static_cast<std::size_t>(b)]; o > 0; o = rig.parent[static_cast<std::size_t>(o)]) chain = pose.r[static_cast<std::size_t>(o)] * chain;
  const glm::vec3 ax = glm::conjugate(chain) * glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
  const double h = angle / 2, sn = std::sin(h);
  const glm::quat q(static_cast<float>(std::cos(h)), static_cast<float>(static_cast<double>(ax.x) * sn), static_cast<float>(static_cast<double>(ax.y) * sn), static_cast<float>(static_cast<double>(ax.z) * sn));
  pose.r[static_cast<std::size_t>(b)] = q * pose.r[static_cast<std::size_t>(b)];
}

double AnimLod::step(double dt, const glm::vec3& pos, const glm::vec3& camera) {
  acc += dt;
  const double dx = static_cast<double>(pos.x - camera.x), dz = static_cast<double>(pos.z - camera.z), d2 = dx * dx + dz * dz;
  const std::uint32_t every = d2 > 90.0 * 90.0 ? 6 : d2 > 45.0 * 45.0 ? 3 : 1;
  if (++tick % every) return 0;
  const double s = acc;
  acc = 0;
  return s;
}

void worldMatrices(const Rig& rig, const Pose& pose, const glm::mat4& root, std::vector<glm::mat4>& out) {
  const std::size_t n = rig.names.size();
  out.resize(n);
  for (std::size_t i = 0; i < n; ++i) {
    const glm::mat4 local = composeTRS(pose.t[i], pose.r[i], pose.s[i]);
    const int p = rig.parent[i];
    out[i] = (p < 0 ? root : out[static_cast<std::size_t>(p)]) * local;
  }
}

}  // namespace rpg::client::anim
