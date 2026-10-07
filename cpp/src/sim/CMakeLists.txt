# rpg_sim: simulação autoritativa. Só depende de rpg_core — nada de rede, janela, GPU, áudio ou UI.
add_library(rpg_sim STATIC
  Sim.cpp
  Items.cpp
  Book.cpp
  Player.cpp
  Combat.cpp
  Enemies.cpp
  Zones.cpp
  Mount.cpp
  Economy.cpp
  Interact.cpp
  WorldSystems.cpp
  World.cpp
)
target_link_libraries(rpg_sim PUBLIC rpg_core)
rpg_set_warnings(rpg_sim)
