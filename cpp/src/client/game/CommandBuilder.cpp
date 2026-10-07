#include "client/game/CommandBuilder.h"

namespace rpg::client {

using protocol::Held;
using protocol::Press;

protocol::PlayerCommand CommandBuilder::build(const InputState& in, const CommandContext& ctx, bool tabChord) {
  protocol::PlayerCommand& c = cmd_;
  c.seq = ++seq_;
  c.camYaw = ctx.camYaw;
  c.held = 0;
  c.moveForward = c.moveRight = 0;
  const bool keys = !ctx.uiOpen && !ctx.typing;
  if (keys) {
    c.moveForward = static_cast<std::int8_t>((in.down(Key::W) ? 1 : 0) - (in.down(Key::S) ? 1 : 0));
    c.moveRight = static_cast<std::int8_t>((in.down(Key::D) ? 1 : 0) - (in.down(Key::A) ? 1 : 0));
    if (in.down(Key::LShift) || in.down(Key::RShift)) c.held |= protocol::kHeldRun;
    if (in.down(Key::LCtrl) || in.down(Key::RCtrl)) c.held |= protocol::kHeldCrouch;
    if (in.right) c.held |= protocol::kHeldAim;
    if (in.hit(Key::Space)) c.press(Press::Jump);
    if (in.hit(Key::C)) c.press(Press::Dodge);
    if (in.hit(Key::Q)) c.press(Press::SkillQ);
    if (in.hit(Key::E)) c.press(Press::SkillE);
    if (in.hit(Key::Digit1) && !in.down(Key::Tab) && !tabChord) c.press(Press::Potion);
    if (in.hit(Key::F)) c.press(Press::Interact);
    if (in.hit(Key::R)) c.press(Press::Mount);
    if (in.leftPressed && in.right) c.press(Press::Fire);
  }
  if (ctx.uiOpen) c.held |= protocol::kHeldUiOpen;
  c.aimYaw = ctx.aim.yaw;
  c.aimPitch = ctx.aim.pitch;
  c.aimDist = ctx.aim.dist;
  c.aimX = ctx.aim.point.x;
  c.aimY = ctx.aim.point.y;
  c.aimZ = ctx.aim.point.z;
  c.aimEnemy = ctx.aim.enemy;
  return c;
}

}  // namespace rpg::client
