# rpg_core: compartilhado por servidor e cliente. Proibido depender de janela, GPU, áudio ou UI
# (conferido por tools/check_layering.py).
add_library(rpg_core STATIC
  Log.cpp
  Paths.cpp
  Sha256.cpp
  Rng.cpp
  JsMath.cpp
  Noise.cpp
  BinaryReader.cpp
  FixedTimestep.cpp
  GameClock.cpp
  data/GameData.cpp
  data/DataHash.cpp
  world/Heightfield.cpp
  world/River.cpp
  world/Terrain.cpp
  world/CollisionGrid.cpp
  world/RouteField.cpp
  world/ZoneMap.cpp
  world/SimPack.cpp
  world/StaticWorld.cpp
  world/GameplayLayout.cpp
  world/SkyLayout.cpp
  movement/MoveEntity.cpp
  movement/CharacterMotor.cpp
  anim/AnimRules.cpp
  protocol/Messages.cpp
  protocol/BookData.cpp
  protocol/SnapshotDelta.cpp
)
target_include_directories(rpg_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(rpg_core
  PUBLIC glm::glm
  PRIVATE nlohmann_json::nlohmann_json
)
rpg_set_warnings(rpg_core)
