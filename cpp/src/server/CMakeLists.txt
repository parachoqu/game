# rpg_server: host da simulação (tick fixo, sessões; persistência e protocolo nas próximas fases).
add_library(rpg_server STATIC
  ServerHost.cpp
)
target_link_libraries(rpg_server PUBLIC rpg_sim rpg_net)
rpg_set_warnings(rpg_server)
