#pragma once
// Inventário (game/inventory.js): pilhas de materiais e equipamentos com condição.
//
// Uma entrada é uma pilha {id, qty} ou um equipamento {id, uid, cond}; `uid != 0` distingue os dois,
// como o `e.uid` do JS. As listas guardam a ordem de inserção, que decide de onde `removeFrom` tira
// (de trás para frente) e a ordem das somas de peso.
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "core/data/GameData.h"

namespace rpg::sim {

struct InvEntry {
  ItemId id;
  int qty = 1;              // pilhas; equipamento conta como 1 (`e.qty || 1`)
  std::uint32_t uid = 0;    // 0 = pilha
  double cond = 100.0;      // equipamento: condição 0–100

  bool isGear() const { return uid != 0; }
  int amount() const { return uid ? 1 : qty; }
  bool operator==(const InvEntry&) const = default;
};
using Gear = InvEntry;
using ItemList = std::vector<InvEntry>;

// Gerador de uid dos equipamentos (inventory.js `uidSeq`), um por mundo.
class UidAllocator {
 public:
  std::uint32_t next() { return next_++; }
  void atLeast(std::uint32_t n) {
    if (n > next_) next_ = n;
  }
  std::uint32_t peek() const { return next_; }

 private:
  std::uint32_t next_ = 1;
};

int count(const ItemList& list, ItemId id);
double weightOf(const GameData& data, const ItemList& list);
Gear makeGear(UidAllocator& uids, ItemId id, double cond = 100.0);
// inventory.js `addTo`: equipamento entra um por um; pilha soma na primeira pilha do mesmo item.
void addTo(const GameData& data, UidAllocator& uids, ItemList& list, ItemId id, int qty = 1,
           std::optional<double> cond = std::nullopt);
// inventory.js `addEntry`: equipamento entra como está; pilha passa por addTo.
void addEntry(const GameData& data, UidAllocator& uids, ItemList& list, const InvEntry& e);
// Remove de trás para frente; devolve quantos saíram.
int removeFrom(ItemList& list, ItemId id, int qty = 1);

// Receita: pares (item, quantidade) na ordem da definição.
using Requirement = std::vector<std::pair<ItemId, int>>;

}  // namespace rpg::sim
