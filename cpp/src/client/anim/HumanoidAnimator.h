#pragma once
// Humanoide da demo (characters.js `makeHumanoid` + `animateHumanoid` + `postura`): clipes reais do
// Mixamo misturados pelo estado (locomoção por direção, corrida, guarda, agachar, pulo, mira com o
// arco, impacto, esquivas, ataques, morte) e camadas procedurais por cima, aplicadas nos ossos em
// espaço do modelo (inércia, idle vivo, mira, montado, derrubado, metamorfose, inclinação).
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>

#include "client/anim/Mixer.h"
#include "core/Rng.h"
#include "core/anim/AnimRules.h"

namespace rpg::client::anim {

using ClipMetaMap = std::map<std::string, PackClipMeta, std::less<>>;

// Estado entregue ao animador (o `s` de characters.js).
struct HumanoidInput {
  double dt = 0.016;
  double mps = 0;
  double attack = -1;  // progresso da ação (-1 = nenhuma)
  rpg::anim::ActionKind kind = rpg::anim::ActionKind::Swing;
  bool mounted = false, down = false;
  double dodge = -1;
  std::string dodgeKey;  // clipe da esquiva ("roll" = rolamento)
  double aim = 0, aimPitch = 0;
  struct Hit {
    rpg::anim::HitSide side = rpg::anim::HitSide::Front;
    double t = 0;
  };
  std::optional<Hit> hit;
  std::optional<double> fwd;
  double strafe = 0;
  std::optional<glm::vec2> moveDir;
  bool channel = false, metamorph = false, craft = false;
  bool crouch = false;
  struct Air {
    double phase = 0;
    bool boosted = false;
  };
  std::optional<Air> air;
  struct Death {
    rpg::anim::DeathFall fall = rpg::anim::DeathFall::Backward;
    double t = 0;
  };
  std::optional<Death> death;
};

class HumanoidAnimator {
 public:
  // race: escala (lados, altura) de RACE; scale: opts.scale. `random` substitui o Math.random da demo
  // (testes de paridade); vazio, vale um Pcg32 com a semente.
  HumanoidAnimator(const Rig& rig, const ClipMetaMap& meta, glm::vec2 race, double scale, std::uint32_t seed,
                   std::function<double()> random = {});

  void setWeapon(const std::string& family);
  const std::string& family() const { return family_; }

  // Devolve false quando o passo foi pulado (personagem longe: anima a cada 3 ou 6 quadros).
  bool animate(const HumanoidInput& s, const AnimWorld& w);

  const Pose& pose() const { return pose_; }
  // Matriz do `body` (rotação procedural, deslocamento e escala da raça) abaixo da raiz.
  glm::mat4 bodyMatrix() const;
  double hipsY() const { return hipsY_; }
  bool hasClip(std::string_view key) const { return actions_.contains(std::string(key)); }

 private:
  int act(std::string_view key) const;
  Action* A(std::string_view key);
  double duration(int a) const { return mixer_.action(a).clip->duration; }
  void overlay(int a, double w);
  void bend(std::string_view key, double x, double y, double z, double angle);
  void ampBone(std::string_view key, double f);
  void upperLayer(std::string_view key, double t, double w);
  void postura(double dt, double w, const AnimWorld& world);

  const Rig& rig_;
  const ClipMetaMap& meta_;
  Mixer mixer_;
  Pose pose_;
  std::unordered_map<std::string, int> actions_;
  std::vector<int> oneShots_;
  int idle_ = -1, walk_ = -1;
  Pcg32 rng_;
  std::function<double()> random_;
  double rand() { return random_ ? random_() : rng_.next(); }
  glm::vec2 race_{1.0f};
  double scale_ = 1;
  std::string family_;
  // corpo (Group entre a raiz e o pivot)
  glm::vec3 bodyRot_{0.0f}, bodyPos_{0.0f};
  // estado do animador (h.*)
  AnimLod lod_;
  double mps_ = 0, smoothAccel_ = 0, fwd_ = 1, strafe_ = 0, walkW_ = 0, prevRunK_ = 0;
  double lean_ = 0, tiltX_ = 0, tiltZ_ = 0;
  double prevYaw_ = 0;  // como na demo: o root nasce com yaw 0, então o primeiro passo já vê a virada
  double phase_ = 0, lookX_ = 0, lookY_ = 0, glanceTimer_ = 0, targetGlanceX_ = 0, targetGlanceY_ = 0;
  double crouchW_ = 0, airW_ = 0, bowW_ = 0;
  std::optional<double> bowT_, bowShotFrom_;
  std::optional<bool> prevCrouch_;
  struct Posture {
    int act = -1;
    double t = 0;
  };
  std::optional<Posture> posture_;
  struct HitLayer {
    std::string key;
    double t = 0, w = 0;
  };
  std::optional<HitLayer> hitLayer_;
  struct DodgeTail {
    int act = -1;
    double t = 0, age = 0;
  };
  std::optional<DodgeTail> dodgeTail_;
  std::optional<std::string> deathKey_;
  double walkDur_ = 1.333, runDur_ = 0.542, hipsY_ = 0;
};

}  // namespace rpg::client::anim
