#pragma once
// Regras puras de escolha de clipe (engine/anim-select.js). Sem three.js e sem modelos: a simulação usa
// `hitSide` e `deathFall` para decidir a reação ao golpe e o lado da queda (enums replicados no
// snapshot); o cliente usa o resto para escolher os clipes que o personagem de fato tem.
//
// Referencial do personagem: o modelo olha para +z; +x é o lado ESQUERDO dele.
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

#include "core/Types.h"

namespace rpg::anim {

// Tipo de ação para o animador (player.js: swing, bow, crossbow, cast, rapid, heavy, thrust, slash,
// spin, charge).
enum class ActionKind : std::uint8_t { Swing, Bow, Crossbow, Cast, Rapid, Heavy, Thrust, Slash, Spin, Charge };

// Pose de uma técnica para o animador (player.js, fim de `updatePlayer`): tipo de ação e duração do
// gesto; `spinStart` > 0 atrasa o giro (giro e ceifa circular só giram depois da preparação).
struct SkillPose {
  ActionKind kind = ActionKind::Swing;
  double duration = 0.6;
  double spinStart = 0;
};
SkillPose skillPose(std::string_view skillKey);
// Progresso [0, 1] do gesto da técnica depois de `t` segundos.
double skillProgress(const SkillPose& pose, double t);

struct LocalDir {
  Real fwd = 0.0;
  Real left = 0.0;
};

// Vetor do mundo (dx, dz) no referencial de quem olha para `yaw`.
LocalDir toLocal(Real yaw, Real dx, Real dz);

// ---------------------------------------------------------------- clipes que um modelo pode ter
enum class Clip : std::uint8_t {
  Walk, WalkBack, StrafeLeft, StrafeRight,
  DodgeForward, DodgeBackward, DodgeLeft, DodgeRight, Roll,
  HitFront, HitBack, HitLeft, HitRight,
  DeathForward, DeathBackward, Down,
  Count,
};
std::string_view clipKey(Clip c);  // nome usado nos registros de clipes da demo ("dodgeForward"…)

using HasClip = std::function<bool(Clip)>;

struct LocomotionWeights {
  Real walk = 1.0, walkBack = 0.0, strafeLeft = 0.0, strafeRight = 0.0;
  bool reverse = false;  // sem clipe para trás: caminhada tocada ao contrário
};
LocomotionWeights locomotionWeights(Real fwd, Real left, const HasClip& has);

// Clipe da esquiva pelo sentido do passo em relação ao corpo; sem o direcional (ou `allow = false`),
// o rolamento. nullopt = nenhum clipe de esquiva.
std::optional<Clip> dodgeClip(Real fwd, Real left, const HasClip& has, bool allow = true);
// Quanto o corpo gira em relação ao sentido da esquiva para o clipe mostrar esse sentido.
Real dodgeFacing(Clip c);

// ---------------------------------------------------------------- impacto e morte
enum class HitSide : std::uint8_t { Front, Back, Left, Right };
enum class DeathFall : std::uint8_t { Backward, Forward };

// Origem do golpe (sem origem: `from` vazio → frente / para trás).
struct HitOrigin {
  Real x = 0.0, z = 0.0;
};

HitSide hitSide(Real victimYaw, Real vx, Real vz, const std::optional<HitOrigin>& from);
std::optional<Clip> hitClip(HitSide side, const HasClip& has);
DeathFall deathFall(Real victimYaw, Real vx, Real vz, const std::optional<HitOrigin>& from);
std::optional<Clip> deathClip(DeathFall fall, const HasClip& has);

}  // namespace rpg::anim
