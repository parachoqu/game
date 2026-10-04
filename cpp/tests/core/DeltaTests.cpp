#include <random>

#include <catch2/catch_test_macros.hpp>

#include "core/Sha256.h"
#include "core/data/DataHash.h"
#include "core/Paths.h"
#include "core/protocol/SnapshotDelta.h"

using namespace rpg;
using namespace rpg::protocol;

namespace {

Snapshot makeSnap(int n, double t) {
  Snapshot s;
  s.world.time = t;
  s.self = PrivateState{};
  s.self->playerId = 1;
  s.self->hp = 100;
  for (int i = 0; i < n; ++i) {
    EntityState e;
    e.id = static_cast<std::uint32_t>(i + 1);
    e.x = i * 2.0;
    s.entities.push_back(e);
  }
  return s;
}

bool same(const Snapshot& a, const Snapshot& b) { return encode(a) == encode(b); }

}  // namespace

TEST_CASE("SHA-256: vetores do FIPS 180-4", "[core][hash]") {
  CHECK(Sha256().hex() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  Sha256 abc;
  abc.update("abc");
  CHECK(abc.hex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  Sha256 longer;
  longer.update("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
  CHECK(longer.hex() == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  Sha256 million;
  const std::string a(1000, 'a');
  for (int i = 0; i < 1000; ++i) million.update(a);
  CHECK(million.hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  // os dados de design têm um resumo estável
  const std::string h = dataHash(pathFromUtf8(RPG_TEST_DATA_DIR));
  CHECK(h.size() == 64);
  CHECK(h == dataHash(pathFromUtf8(RPG_TEST_DATA_DIR)));
}

TEST_CASE("Delta de snapshot: refeito no cliente igual ao snapshot inteiro", "[core][delta]") {
  DeltaEncoder enc;
  DeltaDecoder dec;
  Snapshot s = makeSnap(40, 0);
  // primeiro: completo
  SnapshotDelta d = enc.encode(s, 0);
  CHECK(d.base == 0);
  auto r = dec.decode(decode<SnapshotDelta>(encode(d)).value());
  REQUIRE(r);
  CHECK(same(*r, s));

  // muda duas entidades, tira uma, põe uma: o delta leva só isso
  s.entities[3].x += 1;
  s.entities[10].yaw = 2;
  s.entities.erase(s.entities.begin() + 20);
  EntityState extra;
  extra.id = 999;
  s.entities.push_back(extra);
  s.world.time = 1;
  d = enc.encode(s, dec.lastSeq());
  CHECK(d.base == 1);
  CHECK(d.changed.size() == 3);
  CHECK(d.removed == std::vector<std::uint32_t>{21});
  CHECK(d.selfMode == 0);
  CHECK(d.worldMode == 1);
  CHECK(encode(d).size() < encode(s).size() / 4);
  r = dec.decode(decode<SnapshotDelta>(encode(d)).value());
  REQUIRE(r);
  CHECK(same(*r, s));

  // sem personagem
  s.self.reset();
  d = enc.encode(s, dec.lastSeq());
  CHECK(d.selfMode == 2);
  r = dec.decode(d);
  REQUIRE(r);
  CHECK_FALSE(r->self);
}

TEST_CASE("Delta de snapshot: com perda e atraso, o cliente só refaz o que tem base", "[core][delta]") {
  DeltaEncoder enc;
  DeltaDecoder dec;
  std::mt19937 rng(7);
  Snapshot s = makeSnap(25, 0);
  std::uint32_t acked = 0;
  int applied = 0, dropped = 0;
  for (int tick = 1; tick <= 400; ++tick) {
    for (EntityState& e : s.entities)
      if (rng() % 3 == 0) e.x += 0.1;
    if (tick % 50 == 0) s.entities.pop_back();
    s.world.time = tick;
    const SnapshotDelta d = enc.encode(s, acked);
    if (rng() % 5 == 0) {  // 20% perdidos
      ++dropped;
      continue;
    }
    const auto r = dec.decode(d);
    if (!r) continue;
    REQUIRE(same(*r, s));
    ++applied;
    // a confirmação chega ao servidor com atraso às vezes
    if (rng() % 4 != 0) acked = dec.lastSeq();
  }
  CHECK(applied > 250);
  CHECK(dropped > 40);
}
