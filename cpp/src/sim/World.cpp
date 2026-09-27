#include "sim/World.h"

#include "core/Assert.h"

namespace rpg::sim {

World::World(const GameData& data, std::uint64_t seed) : data_(data), rng_(seed) {}

void World::step(Seconds dt) {
  RPG_ASSERT(dt > 0.0, "World::step: dt precisa ser positivo");
  ++tick_;
  time_ += dt;
}

}  // namespace rpg::sim
