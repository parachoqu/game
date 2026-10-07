#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/SlotMap.h"

namespace {
struct EnemyTag;
using Pool = rpg::SlotMap<std::string, EnemyTag>;

std::vector<std::string> values(const Pool& p) {
  std::vector<std::string> out;
  for (auto id : p.order()) out.push_back(p[id]);
  return out;
}
}  // namespace

TEST_CASE("SlotMap preserva a ordem de inserção ao remover (como splice)", "[core][slotmap]") {
  Pool p;
  const auto a = p.emplace("a");
  const auto b = p.emplace("b");
  const auto c = p.emplace("c");
  const auto d = p.emplace("d");
  CHECK(values(p) == std::vector<std::string>{"a", "b", "c", "d"});

  CHECK(p.erase(b));
  CHECK(values(p) == std::vector<std::string>{"a", "c", "d"});

  // o slot de b é reaproveitado, mas o novo elemento entra no fim da ordem
  const auto e = p.emplace("e");
  CHECK(e.index == b.index);
  CHECK(values(p) == std::vector<std::string>{"a", "c", "d", "e"});
  (void)a, (void)c, (void)d;
}

TEST_CASE("SlotMap: handle antigo deixa de resolver", "[core][slotmap]") {
  Pool p;
  const auto x = p.emplace("x");
  CHECK(p.contains(x));
  CHECK(p.erase(x));
  CHECK_FALSE(p.contains(x));
  CHECK(p.get(x) == nullptr);
  CHECK_FALSE(p.erase(x));

  const auto y = p.emplace("y");
  CHECK(y.index == x.index);
  CHECK(y.generation != x.generation);
  CHECK(p.get(x) == nullptr);  // não aponta para quem ocupou o lugar
  CHECK(*p.get(y) == "y");
  CHECK_FALSE(p.contains(Pool::Id{}));
}

TEST_CASE("Handle empacota e desempacota para o protocolo", "[core][slotmap]") {
  const rpg::Handle<EnemyTag> h{12345, 678};
  CHECK(rpg::Handle<EnemyTag>::unpack(h.packed()) == h);
}
