// Animação dos personagens: amostragem das trilhas e mistura do mixer contra valores calculados à mão,
// e paridade com os animadores da demo (tests/parity/fixtures/anim-parity.json, gravado pelo bake do
// cliente rodando characters.js): mesmos roteiros de entrada → mesma pose em cada nó, mesmo corpo e
// os mesmos vértices com pele no mundo.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include "TestData.h"
#include "client/anim/HumanoidAnimator.h"
#include "client/anim/Mixer.h"
#include "client/anim/QuadrupedAnimator.h"
#include "client/anim/Rig.h"
#include "client/assets/ScenePack.h"
#include "client/entities/CharacterViews.h"
#include "core/Paths.h"

using namespace rpg;
using namespace rpg::client;
using namespace rpg::client::anim;
using Catch::Approx;
using nlohmann::json;

namespace {

std::filesystem::path packDir() { return pathFromUtf8(RPG_TEST_ASSETS_DIR) / "client"; }

bool packAvailable() {
  std::ifstream f(packDir() / "scene.bin", std::ios::binary);
  char head[8] = {};
  return f && f.read(head, 7) && std::string(head, 7) != "version";
}

const ScenePack& pack() {
  static const ScenePack p = ScenePack::load(packDir());
  return p;
}

const Rig& rig(const std::string& id) {
  static std::map<std::string, Rig> rigs;
  auto it = rigs.find(id);
  if (it == rigs.end()) it = rigs.emplace(id, Rig::fromPack(pack(), id)).first;
  return it->second;
}

const json& parity() {
  static const json j = [] {
    std::ifstream f(pathFromUtf8(RPG_TEST_FIXTURES_DIR) / "anim-parity.json");
    return json::parse(f);
  }();
  return j;
}

rpg::anim::ActionKind actionKind(const std::string& k) {
  using K = rpg::anim::ActionKind;
  if (k == "bow") return K::Bow;
  if (k == "crossbow") return K::Crossbow;
  if (k == "cast") return K::Cast;
  if (k == "rapid") return K::Rapid;
  if (k == "heavy") return K::Heavy;
  if (k == "thrust") return K::Thrust;
  if (k == "slash") return K::Slash;
  if (k == "spin") return K::Spin;
  if (k == "charge") return K::Charge;
  return K::Swing;
}

rpg::anim::HitSide hitSide(const std::string& s) {
  using H = rpg::anim::HitSide;
  return s == "back" ? H::Back : s == "left" ? H::Left : s == "right" ? H::Right : H::Front;
}

HumanoidInput humanInput(const json& f, double dt) {
  HumanoidInput s;
  s.dt = dt;
  s.mps = f.value("mps", 0.0);
  s.attack = f.value("attack", -1.0);
  if (f.contains("kind")) s.kind = actionKind(f["kind"].get<std::string>());
  s.mounted = f.value("mounted", false);
  s.down = f.value("down", false);
  s.dodge = f.value("dodge", -1.0);
  if (f.contains("dodgeKey")) s.dodgeKey = f["dodgeKey"].get<std::string>();
  s.aim = f.value("aim", 0.0);
  s.aimPitch = f.value("aimPitch", 0.0);
  if (f.contains("hit")) s.hit = HumanoidInput::Hit{hitSide(f["hit"]["side"].get<std::string>()), f["hit"]["t"].get<double>()};
  if (f.contains("fwd")) s.fwd = f["fwd"].get<double>();
  s.strafe = f.value("strafe", 0.0);
  s.channel = f.value("channel", false);
  s.metamorph = f.value("metamorph", false);
  s.craft = f.value("craft", false);
  s.crouch = f.value("crouch", 0) != 0;
  if (f.contains("air")) s.air = HumanoidInput::Air{f["air"]["phase"].get<double>(), f["air"].value("boosted", false)};
  if (f.contains("death"))
    s.death = HumanoidInput::Death{f["death"]["fall"].get<std::string>() == "forward" ? rpg::anim::DeathFall::Forward : rpg::anim::DeathFall::Backward,
                                   f["death"]["t"].get<double>()};
  return s;
}

// O mesmo `object.matrix` do three: posição, Euler XYZ e escala.
glm::mat4 bodyFromJs(const json& b) {
  const glm::vec3 r(b[0].get<float>(), b[1].get<float>(), b[2].get<float>());
  const glm::vec3 p(b[3].get<float>(), b[4].get<float>(), b[5].get<float>());
  const glm::vec3 s(b[6].get<float>(), b[7].get<float>(), b[8].get<float>());
  return glm::scale(glm::translate(glm::mat4(1.0f), p) * eulerXYZ(r), s);
}

// Ângulo entre duas orientações (rad), sem o sinal do quaternion. 4·asin(|a − b|/2) em double: o
// 2·acos(dot) em float só enxerga degraus de ~1e-3 rad perto de 1 (o erro de arredondamento do dot).
double angleBetween(const glm::quat& a, const glm::quat& b) {
  const double s = glm::dot(a, b) < 0 ? -1.0 : 1.0;
  const auto d = [s](float p, float q) { return static_cast<double>(p) - s * static_cast<double>(q); };
  const double dx = d(a.x, b.x), dy = d(a.y, b.y), dz = d(a.z, b.z), dw = d(a.w, b.w);
  return 4.0 * std::asin(std::min(1.0, std::sqrt(dx * dx + dy * dy + dz * dz + dw * dw) / 2.0));
}

struct PoseDiff {
  double angle = 0, pos = 0, scale = 0;
  std::string worst, worstPos;
};

PoseDiff comparePose(const Rig& r, const Pose& pose, const json& nodes) {
  PoseDiff d;
  REQUIRE(nodes.size() == r.names.size());
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    const json& n = nodes[i];
    const glm::quat q(n[3].get<float>(), n[0].get<float>(), n[1].get<float>(), n[2].get<float>());
    const glm::vec3 t(n[4].get<float>(), n[5].get<float>(), n[6].get<float>());
    const glm::vec3 s(n[7].get<float>(), n[8].get<float>(), n[9].get<float>());
    const double a = angleBetween(q, pose.r[i]);
    if (a > d.angle) {
      d.angle = a;
      d.worst = r.names[i];
    }
    const double dp = static_cast<double>(glm::length(t - pose.t[i]));
    if (dp > d.pos) {
      d.pos = dp;
      d.worstPos = r.names[i] + " js(" + std::to_string(t.x) + "," + std::to_string(t.y) + "," + std::to_string(t.z) + ") c++(" +
                   std::to_string(pose.t[i].x) + "," + std::to_string(pose.t[i].y) + "," + std::to_string(pose.t[i].z) + ") rest(" +
                   std::to_string(r.rest.t[i].x) + "," + std::to_string(r.rest.t[i].y) + "," + std::to_string(r.rest.t[i].z) + ")";
    }
    d.scale = std::max(d.scale, static_cast<double>(glm::length(s - pose.s[i])));
  }
  return d;
}

double maxDiff(const glm::mat4& a, const glm::mat4& b) {
  double m = 0;
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r) m = std::max(m, static_cast<double>(std::abs(a[c][r] - b[c][r])));
  return m;
}

// Vértices com pele no mundo (SkinnedMesh.getVertexPosition + matrixWorld do three).
double skinError(const Rig& r, const Pose& pose, const glm::mat4& rootBody, const json& skin) {
  std::vector<glm::mat4> world;
  worldMatrices(r, pose, rootBody, world);
  REQUIRE(skin["mesh"].get<std::size_t>() < r.meshes.size());
  const RigMesh& m = r.meshes[skin["mesh"].get<std::size_t>()];
  REQUIRE(m.skinned);
  const PackGeometry& g = pack().geometries[static_cast<std::size_t>(m.geometry)];
  const std::vector<float> pos = pack().attributeFloats(g, "position");
  const std::vector<float> ji = pack().attributeFloats(g, "skinIndex");
  const std::vector<float> jw = pack().attributeFloats(g, "skinWeight");
  std::vector<glm::mat4> palette(m.bones.size());
  for (std::size_t b = 0; b < m.bones.size(); ++b) palette[b] = world[static_cast<std::size_t>(m.bones[b])] * m.inverses[b] * m.bind;
  double err = 0;
  for (const json& v : skin["vertices"]) {
    const auto i = v[0].get<std::size_t>();
    const glm::vec4 p(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2], 1.0f);
    glm::vec4 w(0.0f);
    for (std::size_t k = 0; k < 4; ++k) w += palette[static_cast<std::size_t>(ji[i * 4 + k])] * p * jw[i * 4 + k];
    const glm::vec3 want(v[1].get<float>(), v[2].get<float>(), v[3].get<float>());
    err = std::max(err, static_cast<double>(glm::length(glm::vec3(w) - want)));
  }
  return err;
}

// Poses gravadas no meio do roteiro (quadro → pose), além da do fim.
std::map<std::size_t, const json*> checkpointsOf(const json& rec) {
  std::map<std::size_t, const json*> out;
  if (rec.contains("checkpoints"))
    for (const json& c : rec["checkpoints"]) out[c["frame"].get<std::size_t>()] = &c;
  return out;
}

glm::mat4 rootAt(double x, double y, double z, double yaw) {
  return glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z))),
                     static_cast<float>(yaw), glm::vec3(0, 1, 0));
}

}  // namespace

TEST_CASE("Animação: trilhas com os interpolantes do three", "[client][anim]") {
  Track t;
  t.path = Track::Path::Position;
  t.size = 3;
  t.times = {0.0f, 1.0f, 2.0f};
  t.values = {0, 0, 0, 10, 20, 30, 20, 0, 0};
  float v[4];
  sampleTrack(t, -1, v);  // antes do primeiro quadro: o primeiro
  CHECK(v[0] == 0.0f);
  sampleTrack(t, 0.5, v);
  CHECK(v[0] == Approx(5));
  CHECK(v[1] == Approx(10));
  sampleTrack(t, 1.5, v);
  CHECK(v[0] == Approx(15));
  CHECK(v[1] == Approx(10));
  sampleTrack(t, 9, v);  // depois do último: o último
  CHECK(v[0] == 20.0f);
  t.step = true;
  sampleTrack(t, 1.9, v);
  CHECK(v[0] == 10.0f);

  // quaternions por slerp (o caminho mais curto, como o slerpFlat)
  Track q;
  q.path = Track::Path::Quaternion;
  q.size = 4;
  q.times = {0.0f, 1.0f};
  const float h = std::sqrt(0.5f);
  q.values = {0, 0, 0, 1, 0, h, 0, h};  // 0° → 90° em y
  sampleTrack(q, 0.5, v);
  const glm::quat r(v[3], v[0], v[1], v[2]);
  CHECK(glm::degrees(glm::angle(r)) == Approx(45).margin(1e-3));
  CHECK(boneKey("mixamorig:LeftArm") == "LeftArm");
  CHECK(boneKey("mixamorig9LeftArm") == "LeftArm");
  CHECK(boneKey("Head") == "Head");
}

TEST_CASE("Animação: mixer acumula por peso e completa com a pose de repouso", "[client][anim]") {
  Rig r;
  r.names = {"pivot", "bone"};
  r.parent = {-1, 0};
  r.isBone = {false, true};
  r.rest.t = {glm::vec3(0.0f), glm::vec3(1, 0, 0)};
  r.rest.s = {glm::vec3(1.0f), glm::vec3(1.0f)};
  r.rest.r = {glm::quat(1, 0, 0, 0), glm::quat(1, 0, 0, 0)};
  const auto clip = [](float x) {
    Clip c;
    c.duration = 1;
    Track t;
    t.node = 1;
    t.path = Track::Path::Position;
    t.times = {0.0f, 1.0f};
    t.values = {x, 0, 0, x, 0, 0};
    c.tracks.push_back(t);
    return c;
  };
  const Clip a = clip(10), b = clip(20);
  Mixer m(r);
  const int ia = m.add(&a), ib = m.add(&b);
  Pose p = r.rest;
  m.action(ia).weight = 0.25;
  m.action(ib).weight = 0.25;
  m.update(0, p);
  // (10·0,25 + 20·0,25) / 0,5 = 15; depois 15·0,5 + 1·0,5 = 8
  CHECK(p.t[1].x == Approx(8));
  m.action(ia).weight = 0;
  m.action(ib).weight = 1;
  m.update(0.3, p);
  CHECK(p.t[1].x == Approx(20));
  CHECK(m.action(ia).time == Approx(0.3));  // o tempo anda mesmo com peso zero
  m.update(0.8, p);
  CHECK(m.action(ia).time == Approx(0.1));  // LoopRepeat
  m.action(ib).weight = 0;
  m.update(0, p);
  CHECK(p.t[1].x == 1.0f);  // sem peso: a pose original
}

TEST_CASE("Animação: humanoides iguais aos da demo", "[client][anim][parity]") {
  if (!packAvailable()) SKIP("cpp/assets/client ausente");
  const json& P = parity();
  const double dt = P["dt"].get<double>(), x = P["x"].get<double>(), y = P["y"].get<double>(), z = P["z"].get<double>();
  const StaticMap& map = rpg::test::staticWorld().map(MapKind::Region);
  REQUIRE(P["humanoids"].size() >= 3);
  for (const json& rec : P["humanoids"]) {
    const std::string model = rec["model"], scenario = rec["scenario"];
    CAPTURE(model, scenario);
    const Rig& r = rig(model);
    HumanoidAnimator h(r, pack().clipMeta, raceScale(rec["race"].get<std::string>()), 1.0, 0, [] { return 0.5; });
    h.setWeapon(rec["family"].is_null() ? std::string() : rec["family"].get<std::string>());
    AnimWorld w;
    w.rootPos = glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
    w.camera = w.rootPos;
    w.groundHeight = [&map](double gx, double gz) { return map.groundHeight(gx, gz); };
    double yaw = 0;
    const auto checkpoints = checkpointsOf(rec);
    std::size_t frame = 0;
    for (const json& f : rec["frames"]) {
      yaw = f["yaw"].get<double>();
      w.rootYaw = yaw;
      REQUIRE(h.animate(humanInput(f["s"], dt), w));
      if (const auto it = checkpoints.find(frame); it != checkpoints.end()) {
        const PoseDiff c = comparePose(r, h.pose(), (*it->second)["nodes"]);
        CAPTURE(frame, c.worst, c.angle, c.pos, c.worstPos);
        CHECK(c.angle < 2e-3);
        CHECK(c.pos < 2e-3);
        CHECK(maxDiff(h.bodyMatrix(), bodyFromJs((*it->second)["body"])) < 1e-4);
      }
      ++frame;
    }
    const PoseDiff d = comparePose(r, h.pose(), rec["pose"]["nodes"]);
    CAPTURE(d.worst, d.angle, d.pos, d.worstPos);
    CHECK(d.angle < 2e-3);
    CHECK(d.pos < 2e-3);
    CHECK(d.scale < 1e-4);
    CHECK(maxDiff(h.bodyMatrix(), bodyFromJs(rec["pose"]["body"])) < 1e-4);
    if (rec.contains("skin") && !rec["skin"].is_null()) {
      const double e = skinError(r, h.pose(), rootAt(x, y, z, yaw) * h.bodyMatrix(), rec["skin"]);
      CAPTURE(e);
      CHECK(e < 2e-3);
    }
  }
}

TEST_CASE("Animação: quadrúpedes iguais aos da demo", "[client][anim][parity]") {
  if (!packAvailable()) SKIP("cpp/assets/client ausente");
  const json& P = parity();
  const double dt = P["dt"].get<double>(), x = P["x"].get<double>(), y = P["y"].get<double>(), z = P["z"].get<double>();
  const StaticMap& map = rpg::test::staticWorld().map(MapKind::Region);
  for (const json& rec : P["quadrupeds"]) {
    const std::string id = rec["rig"], kind = rec["kind"], scenario = rec.value("scenario", std::string("base"));
    CAPTURE(id, scenario);
    const auto checkpoints = checkpointsOf(rec);
    std::size_t frame = 0;
    const Rig& r = rig(id);
    const QuadrupedAnimator::Kind k = kind == "mount" ? QuadrupedAnimator::Kind::Mount : id == "hound" ? QuadrupedAnimator::Kind::Hound : QuadrupedAnimator::Kind::Beast;
    QuadrupedAnimator a(r, k, rec["scale"].get<double>(), 0, [] { return 0.5; });
    AnimWorld w;
    w.rootPos = glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
    w.camera = w.rootPos;
    w.groundHeight = [&map](double gx, double gz) { return map.groundHeight(gx, gz); };
    double yaw = 0;
    for (const json& f : rec["frames"]) {
      yaw = f["yaw"].get<double>();
      w.rootYaw = yaw;
      const json& s = f["s"];
      QuadrupedInput in;
      in.dt = dt;
      in.mps = s.value("mps", 0.0);
      in.down = s.value("down", false);
      in.stun = s.value("stun", false);
      in.windup = s.value("windup", -1.0);
      in.attack = s.value("attack", -1.0);
      if (k == QuadrupedAnimator::Kind::Mount) in = QuadrupedInput{dt, s.value("mps", 0.0) / 12.5 * 12.5, false, false, -1, -1};
      REQUIRE(a.animate(in, w));
      if (const auto it = checkpoints.find(frame); it != checkpoints.end()) {
        const PoseDiff c = comparePose(r, a.pose(), (*it->second)["nodes"]);
        CAPTURE(frame, c.worst, c.angle, c.pos, c.worstPos);
        CHECK(c.angle < 2e-3);
        CHECK(c.pos < 2e-3);
        CHECK(maxDiff(a.bodyMatrix(), bodyFromJs((*it->second)["body"])) < 1e-4);
      }
      ++frame;
    }
    const PoseDiff d = comparePose(r, a.pose(), rec["pose"]["nodes"]);
    CAPTURE(d.worst, d.angle, d.pos, d.worstPos);
    CHECK(d.angle < 2e-3);
    CHECK(d.pos < 2e-3);
    CHECK(maxDiff(a.bodyMatrix(), bodyFromJs(rec["pose"]["body"])) < 1e-4);
    if (rec.contains("skin") && !rec["skin"].is_null()) {
      const double e = skinError(r, a.pose(), rootAt(x, y, z, yaw) * a.bodyMatrix(), rec["skin"]);
      CAPTURE(e);
      CHECK(e < 2e-3);
    }
  }
}
