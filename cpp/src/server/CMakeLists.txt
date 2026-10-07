# rpg_server: host da simulação (tick fixo, sessões) e persistência (registros em JSON).
add_library(rpg_server STATIC
  ServerHost.cpp
  CommandValidator.cpp
  persist/SaveRecords.cpp
  persist/SaveStore.cpp
)
target_link_libraries(rpg_server PUBLIC rpg_sim rpg_net nlohmann_json::nlohmann_json)
rpg_set_warnings(rpg_server)
