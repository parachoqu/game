add_executable(rpg_server_app main.cpp)
set_target_properties(rpg_server_app PROPERTIES OUTPUT_NAME rpg_server)
target_link_libraries(rpg_server_app PRIVATE rpg_server)
target_compile_definitions(rpg_server_app PRIVATE
  RPG_DEFAULT_DATA_DIR="${RPG_DATA_DIR}"
  RPG_DEFAULT_SIM_DIR="${RPG_SIM_DIR}"
)
rpg_set_warnings(rpg_server_app)
