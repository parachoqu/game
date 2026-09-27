# rpg_core: compartilhado por servidor e cliente. Proibido depender de janela, GPU, áudio ou UI
# (conferido por tools/check_layering.py).
add_library(rpg_core STATIC
  Log.cpp
  Rng.cpp
  Noise.cpp
  BinaryReader.cpp
  FixedTimestep.cpp
  GameClock.cpp
  data/GameData.cpp
  world/Heightfield.cpp
  world/River.cpp
  world/Terrain.cpp
  world/CollisionGrid.cpp
  world/RouteField.cpp
  world/ZoneMap.cpp
  world/SimPack.cpp
  world/StaticWorld.cpp
  movement/MoveEntity.cpp
)
target_include_directories(rpg_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(rpg_core
  PUBLIC glm::glm
  PRIVATE nlohmann_json::nlohmann_json
)
rpg_set_warnings(rpg_core)
