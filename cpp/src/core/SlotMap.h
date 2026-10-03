#pragma once
// Pool tipado com handles geracionais e ordem de inserção estável.
//
// A ordem importa: no JS as entidades ficam em arrays (`G.enemies`, `G.lootBags`…) e várias regras
// dependem da ordem de iteração (quem é atingido primeiro, `separate()` empurrando pares, `find()` do
// primeiro saco de carga). Remover preserva a ordem relativa, como o `splice` do JS; `order()` devolve
// os handles vivos nessa ordem.
//
// Remover durante a iteração invalida o span de `order()`: itere de trás para frente por índice (como
// os laços `for (let i = length - 1; …)` da demo) ou sobre uma cópia.
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "core/Assert.h"
#include "core/Handle.h"

namespace rpg {

template <class T, class Tag>
class SlotMap {
 public:
  using Id = Handle<Tag>;

  template <class... Args>
  Id emplace(Args&&... args) {
    std::uint32_t index;
    if (!free_.empty()) {
      index = free_.back();
      free_.pop_back();
    } else {
      index = static_cast<std::uint32_t>(slots_.size());
      slots_.emplace_back();
    }
    Slot& s = slots_[index];
    s.value.emplace(std::forward<Args>(args)...);
    const Id id{index, s.generation};
    order_.push_back(id);
    return id;
  }

  // Remove e devolve true se o handle ainda era válido.
  bool erase(Id id) {
    if (!contains(id)) return false;
    Slot& s = slots_[id.index];
    s.value.reset();
    s.generation = s.generation + 1 == 0 ? 1 : s.generation + 1;  // 0 fica reservado ao handle nulo
    free_.push_back(id.index);
    order_.erase(std::find(order_.begin(), order_.end(), id));
    return true;
  }

  bool contains(Id id) const {
    return id.valid() && id.index < slots_.size() && slots_[id.index].generation == id.generation &&
           slots_[id.index].value.has_value();
  }

  T* get(Id id) { return contains(id) ? &*slots_[id.index].value : nullptr; }
  const T* get(Id id) const { return contains(id) ? &*slots_[id.index].value : nullptr; }

  T& operator[](Id id) {
    RPG_ASSERT(contains(id), "SlotMap: handle inválido");
    return *slots_[id.index].value;
  }
  const T& operator[](Id id) const {
    RPG_ASSERT(contains(id), "SlotMap: handle inválido");
    return *slots_[id.index].value;
  }

  std::span<const Id> order() const { return order_; }
  std::size_t size() const { return order_.size(); }
  bool empty() const { return order_.empty(); }

  void clear() {
    for (const Id id : std::vector<Id>(order_)) erase(id);
  }

 private:
  struct Slot {
    std::optional<T> value;
    std::uint32_t generation = 1;
  };
  std::vector<Slot> slots_;
  std::vector<std::uint32_t> free_;
  std::vector<Id> order_;
};

}  // namespace rpg
