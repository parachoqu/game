#include "sim/Items.h"

namespace rpg::sim {

int count(const ItemList& list, ItemId id) {
  int n = 0;
  for (const InvEntry& e : list)
    if (e.id == id) n += e.amount();
  return n;
}

double weightOf(const GameData& data, const ItemList& list) {
  double w = 0.0;
  for (const InvEntry& e : list) w += data.items[e.id].weight * e.amount();
  return w;
}

Gear makeGear(UidAllocator& uids, ItemId id, double cond) {
  Gear g;
  g.id = id;
  g.uid = uids.next();
  g.cond = cond;
  return g;
}

void addTo(const GameData& data, UidAllocator& uids, ItemList& list, ItemId id, int qty, std::optional<double> cond) {
  if (data.items[id].isGear()) {
    for (int i = 0; i < qty; ++i) list.push_back(makeGear(uids, id, cond.value_or(100.0)));
    return;
  }
  for (InvEntry& e : list) {
    if (e.id == id && !e.uid) {
      e.qty += qty;
      return;
    }
  }
  InvEntry e;
  e.id = id;
  e.qty = qty;
  list.push_back(e);
}

void addEntry(const GameData& data, UidAllocator& uids, ItemList& list, const InvEntry& e) {
  if (e.uid) list.push_back(e);
  else addTo(data, uids, list, e.id, e.qty);
}

int removeFrom(ItemList& list, ItemId id, int qty) {
  int left = qty;
  for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(list.size()) - 1; i >= 0 && left > 0; --i) {
    InvEntry& e = list[static_cast<std::size_t>(i)];
    if (e.id != id) continue;
    if (e.uid) {
      list.erase(list.begin() + i);
      left--;
    } else {
      const int take = e.qty < left ? e.qty : left;
      e.qty -= take;
      left -= take;
      if (e.qty <= 0) list.erase(list.begin() + i);
    }
  }
  return qty - left;
}

}  // namespace rpg::sim
