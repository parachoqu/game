find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(rpg_tests
  core/MathTests.cpp
  core/RngTests.cpp
  core/GameClockTests.cpp
  core/SlotMapTests.cpp
  core/FixedTimestepTests.cpp
  core/GameDataTests.cpp
  net/LocalTransportTests.cpp
  sim/WorldTests.cpp
  server/ServerHostTests.cpp
)
target_link_libraries(rpg_tests PRIVATE rpg_server Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
target_compile_definitions(rpg_tests PRIVATE RPG_TEST_DATA_DIR="${RPG_DATA_DIR}")
rpg_set_warnings(rpg_tests)
if(WIN32)
  # Os nomes dos casos têm acentos; no Windows a página de código do console atrapalha a descoberta.
  add_test(NAME rpg_tests COMMAND rpg_tests)
else()
  catch_discover_tests(rpg_tests)
endif()

# Separação de camadas (ARQUITETURA.md, seção 3).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME arch.layering COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/check_layering.py" "${PROJECT_SOURCE_DIR}")
endif()

# cpp/data em dia com demo/src/config.js (a demo é a fonte até a migração terminar).
find_program(NODE_EXECUTABLE node)
set(_export "${PROJECT_SOURCE_DIR}/../demo/tools/export-game-data.mjs")
if(NODE_EXECUTABLE AND EXISTS "${_export}")
  add_test(NAME data.in_sync_with_demo COMMAND "${NODE_EXECUTABLE}" "${_export}" --check)
endif()

# O servidor headless sobe, valida os dados e roda 10 s de simulação.
add_test(NAME smoke.server_headless COMMAND rpg_server_app --data "${RPG_DATA_DIR}" --ticks 300)
