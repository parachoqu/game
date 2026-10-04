#include "core/protocol/SnapshotDelta.h"

#include <algorithm>
#include <unordered_set>

namespace rpg::protocol {

SnapshotDelta DeltaEncoder::encode(const Snapshot& snap, std::uint32_t acked) {
  Sent cur;
  cur.seq = ++seq_;
  cur.world = protocol::encode(snap.world);
  cur.hasSelf = snap.self.has_value();
  if (snap.self) cur.self = protocol::encode(*snap.self);
  for (const EntityState& e : snap.entities) cur.entities.emplace(e.id, protocol::encode(e));

  const Sent* base = nullptr;
  if (acked != 0)
    for (const Sent& s : history_)
      if (s.seq == acked) base = &s;

  SnapshotDelta d;
  d.seq = cur.seq;
  if (!base) {
    d.base = 0;
    d.worldMode = 1;
    d.world = snap.world;
    d.selfMode = snap.self ? 1 : 2;
    if (snap.self) d.self = *snap.self;
    d.changed = snap.entities;
  } else {
    d.base = base->seq;
    d.worldMode = cur.world == base->world ? 0 : 1;
    if (d.worldMode) d.world = snap.world;
    if (!snap.self) d.selfMode = 2;
    else if (base->hasSelf && cur.self == base->self) d.selfMode = 0;
    else {
      d.selfMode = 1;
      d.self = *snap.self;
    }
    for (const EntityState& e : snap.entities) {
      const auto it = base->entities.find(e.id);
      if (it == base->entities.end() || it->second != cur.entities.at(e.id)) d.changed.push_back(e);
    }
    for (const auto& [id, bytes] : base->entities)
      if (!cur.entities.contains(id)) d.removed.push_back(id);
    std::sort(d.removed.begin(), d.removed.end());
  }
  history_.push_back(std::move(cur));
  while (history_.size() > kHistory) history_.pop_front();
  return d;
}

std::optional<Snapshot> DeltaDecoder::decode(const SnapshotDelta& d) {
  if (d.seq <= last_) return std::nullopt;  // atrasado ou repetido
  Snapshot out;
  if (d.base == 0) {
    out.world = d.world;
    if (d.selfMode == 1) out.self = d.self;
    out.entities = d.changed;
  } else {
    const Snapshot* base = nullptr;
    for (const auto& [seq, s] : history_)
      if (seq == d.base) base = &s;
    if (!base) return std::nullopt;
    out.world = d.worldMode == 1 ? d.world : base->world;
    if (d.selfMode == 0) out.self = base->self;
    else if (d.selfMode == 1) out.self = d.self;
    // a ordem das entidades segue a da base, com as novas no fim (como o servidor as listou)
    std::unordered_map<std::uint32_t, const EntityState*> changed;
    for (const EntityState& e : d.changed) changed.emplace(e.id, &e);
    const std::unordered_set<std::uint32_t> removed(d.removed.begin(), d.removed.end());
    out.entities.reserve(base->entities.size() + d.changed.size());
    for (const EntityState& e : base->entities) {
      if (removed.contains(e.id)) continue;
      const auto it = changed.find(e.id);
      if (it != changed.end()) {
        out.entities.push_back(*it->second);
        changed.erase(it);
      } else {
        out.entities.push_back(e);
      }
    }
    for (const EntityState& e : d.changed)
      if (changed.contains(e.id)) out.entities.push_back(e);
  }
  last_ = d.seq;
  history_.emplace_back(d.seq, out);
  while (history_.size() > kHistory) history_.pop_front();
  return out;
}

}  // namespace rpg::protocol
