// Regras de escolha de clipe: os mesmos casos de demo/tests/animation.test.js.
#include <set>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/Math.h"
#include "core/anim/AnimRules.h"

using namespace rpg::anim;
using Catch::Approx;

namespace {

HasClip with(std::set<Clip> clips) {
  return [clips](Clip c) { return clips.contains(c); };
}

}  // namespace

TEST_CASE("toLocal: frente e lado esquerdo no referencial do personagem", "[core][anim]") {
  const LocalDir ahead = toLocal(0, 0, 1);
  CHECK(ahead.fwd == Approx(1));
  CHECK(ahead.left == Approx(0).margin(1e-12));
  const LocalDir left = toLocal(0, 1, 0);  // +x é o lado esquerdo de quem olha para +z
  CHECK(left.left == Approx(1));
}

TEST_CASE("locomotionWeights: diagonais dividem o peso; sem clipe, volta à caminhada", "[core][anim]") {
  const auto all = with({Clip::WalkBack, Clip::StrafeLeft, Clip::StrafeRight});
  const LocomotionWeights w = locomotionWeights(1, 1, all);
  CHECK(w.walk == Approx(0.5));
  CHECK(w.strafeLeft == Approx(0.5));
  CHECK(w.walk + w.walkBack + w.strafeLeft + w.strafeRight == Approx(1));
  const LocomotionWeights back = locomotionWeights(-1, 0, with({}));
  CHECK(back.reverse);
  CHECK(back.walk == Approx(1));
  CHECK(locomotionWeights(0, 0, all).walk == 1);
}

TEST_CASE("dodgeClip: direcional quando existe e combina com a arma; senão rolamento", "[core][anim]") {
  const auto full = with({Clip::DodgeForward, Clip::DodgeBackward, Clip::DodgeLeft, Clip::DodgeRight, Clip::Roll});
  CHECK(dodgeClip(1, 0, full) == Clip::DodgeForward);
  CHECK(dodgeClip(-1, 0.2, full) == Clip::DodgeBackward);
  CHECK(dodgeClip(0.1, 1, full) == Clip::DodgeLeft);
  CHECK(dodgeClip(0.1, -1, full) == Clip::DodgeRight);
  CHECK(dodgeClip(1, 0, full, false) == Clip::Roll);
  CHECK_FALSE(dodgeClip(1, 0, with({})).has_value());
  CHECK(dodgeFacing(Clip::DodgeBackward) == Approx(rpg::kPi));
}

TEST_CASE("hitSide e deathFall: eixo dominante do golpe", "[core][anim]") {
  CHECK(hitSide(0, 0, 0, HitOrigin{0, 5}) == HitSide::Front);
  CHECK(hitSide(0, 0, 0, HitOrigin{0, -5}) == HitSide::Back);
  CHECK(hitSide(0, 0, 0, HitOrigin{5, 0}) == HitSide::Left);
  CHECK(hitSide(0, 0, 0, HitOrigin{-5, 0}) == HitSide::Right);
  CHECK(hitSide(0, 0, 0, std::nullopt) == HitSide::Front);
  CHECK(deathFall(0, 0, 0, HitOrigin{0, 5}) == DeathFall::Backward);
  CHECK(deathFall(0, 0, 0, HitOrigin{0, -5}) == DeathFall::Forward);
  CHECK(deathClip(DeathFall::Forward, with({Clip::Down})) == Clip::Down);
  CHECK_FALSE(hitClip(HitSide::Left, with({Clip::HitFront})).has_value());
}

TEST_CASE("skillPose: tipo e duração do gesto de cada técnica", "[core][anim]") {
  CHECK(skillPose("giro").kind == ActionKind::Spin);
  CHECK(skillProgress(skillPose("giro"), 0.2) == 0);
  CHECK(skillProgress(skillPose("giro"), 0.55) == Approx(0.5));
  CHECK(skillPose("setaGelo").kind == ActionKind::Cast);
  CHECK(skillPose("tiroBesta").kind == ActionKind::Crossbow);
  CHECK(skillPose("machadadaPesada").duration == Approx(0.75));
  CHECK(skillPose("desconhecida").duration == Approx(0.6));
}
