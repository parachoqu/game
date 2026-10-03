// Montaria de carga como item equipado (game/mount.js): chamar, montar e desmontar exigem condição
// segura. Cada jogador tem o seu animal, que continua no mundo quando ele desmonta.
#include <algorithm>
#include <cmath>

#include "core/JsMath.h"
#include "core/Math.h"
#include "core/movement/MoveEntity.h"
#include "sim/Sim.h"

namespace rpg::sim {

using protocol::MessageId;
using protocol::MsgArg;
using protocol::SoundId;
using protocol::ToastKind;

void spawnMountNear(Sim& s, Player& p) {
  MountState& m = p.mount;
  const double side = p.yaw - kPi / 2;
  m.pos = {p.pos.x + js::sin(side) * 2.5, p.pos.z + js::cos(side) * 2.5};
  m.y = 0;
  const StaticMap& map = s.mapOf(MapKind::Region);
  moveEntity(map, m.pos, 0, 0, 1);
  m.y = map.groundHeight(m.pos.x, m.pos.z);
  m.yaw = p.yaw;
  m.present = true;
}

void despawnMount(Sim& s, Player& p) {
  if (p.mounted) dismount(s, p, true);
  p.mount.present = false;
  // alforjes voltam com o animal para o estábulo
  if (!p.saddle.empty()) {
    p.stableBags.insert(p.stableBags.end(), p.saddle.begin(), p.saddle.end());
    p.saddle.clear();
  }
}

void mountAction(Sim& s, Player& p) {
  if (p.mounted) {
    dismount(s, p, false);
    return;
  }
  if (!p.equip.mount) {
    s.toast(p, MessageId::MountNone, {}, ToastKind::Info);
    return;
  }
  if (p.zone == ZoneKind::Turbulenta) {
    s.toast(p, MessageId::MountNoTurbulent, {}, ToastKind::Warn);
    return;
  }
  if (inCombat(s, p)) {
    s.toast(p, MessageId::MountInCombat, {}, ToastKind::Warn);
    s.sfx(p, SoundId::Deny);
    return;
  }
  if (p.state != PlayerState::Free) return;
  const MountState& m = p.mount;
  if (m.present) {
    const double d = jsHypot(m.pos.x - p.pos.x, m.pos.z - p.pos.z);
    if (d < 5) {
      startChannel(p, MessageId::ChannelMounting, 1.0, ChannelAction::MountUp, 0, true, false);
    } else if (d < 80) {
      startChannel(p, MessageId::ChannelCallingMount, 2.5, ChannelAction::CallMount, 0, true, false);
    } else {
      s.toast(p, MessageId::MountTooFar,
              {MsgArg::integer(static_cast<long long>(jsRound(d))),
               MsgArg::ref(MsgArg::Type::Place, static_cast<std::uint16_t>(placeOf(s, MapKind::Region, m.pos.x, m.pos.z)))},
              ToastKind::Warn);
    }
  } else {
    startChannel(p, MessageId::ChannelCallingMount, 2.0, ChannelAction::CallMount, 0, true, false);
  }
}

void updateMount(Sim& s, Player& p) {
  MountState& m = p.mount;
  if (!m.present) return;
  if (p.mounted) {
    m.pos = {p.pos.x, p.pos.z};
    m.yaw = p.yaw;
  }
  m.y = s.mapOf(MapKind::Region).groundHeight(m.pos.x, m.pos.z);
}

}  // namespace rpg::sim
