#include "client/ClientWorld.h"

#include "client/net/ClientSession.h"

namespace rpg::client {

void ClientWorld::set(std::uint32_t playerId, std::vector<protocol::EntityState> entities,
                      const protocol::Snapshot* latest) {
  playerId_ = playerId;
  entities_ = std::move(entities);
  index_.clear();
  for (std::size_t i = 0; i < entities_.size(); ++i) index_[entities_[i].id] = i;
  if (latest) latest_ = *latest;
  else latest_.reset();
}

void ClientWorld::update(ClientSession& session, double localTime) {
  SnapshotInterpolator& snaps = session.snapshots();
  set(session.playerId(), snaps.sample(localTime), snaps.latest());
}

const protocol::EntityState* ClientWorld::find(std::uint32_t id) const {
  const auto it = index_.find(id);
  return it == index_.end() ? nullptr : &entities_[it->second];
}

void ClientWorld::placeSelf(double x, double y, double z) {
  const auto it = index_.find(playerId_);
  if (it == index_.end()) return;
  protocol::EntityState& e = entities_[it->second];
  e.x = x;
  e.y = y;
  e.z = z;
}

MapKind ClientWorld::map() const {
  const protocol::EntityState* m = me();
  return m ? static_cast<MapKind>(m->map) : MapKind::Region;
}

std::uint32_t ClientWorld::instance() const {
  const protocol::EntityState* m = me();
  return m ? m->instance : 0;
}

}  // namespace rpg::client
