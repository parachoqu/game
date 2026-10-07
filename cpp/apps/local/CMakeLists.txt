# rpg_local: o jogo local (servidor em thread + cliente). Único alvo que linka os dois lados.
add_executable(rpg_local main.cpp)
target_link_libraries(rpg_local PRIVATE rpg_client rpg_server)
target_compile_definitions(rpg_local PRIVATE
  RPG_DEFAULT_DATA_DIR="${RPG_DATA_DIR}"
  RPG_DEFAULT_SIM_DIR="${RPG_SIM_DIR}"
  RPG_DEFAULT_ASSETS_DIR="${RPG_ASSETS_DIR}"
)
rpg_set_warnings(rpg_local)
