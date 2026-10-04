# rpg_net: transporte. Não conhece simulação nem cliente; só bytes, canais e conexões.
add_library(rpg_net STATIC
  LocalTransport.cpp
  EnetTransport.cpp
)
target_link_libraries(rpg_net PUBLIC rpg_core PRIVATE rpg::enet)
rpg_set_warnings(rpg_net)
