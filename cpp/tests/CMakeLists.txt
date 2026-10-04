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
  core/CollisionTests.cpp
  core/TerrainTests.cpp
  core/ProtocolTests.cpp
  core/AnimRulesTests.cpp
  parity/WorldParityTests.cpp
  parity/SimParityTests.cpp
  sim/WorldTests.cpp
  server/ServerHostTests.cpp
  server/PersistenceTests.cpp
  server/EndToEndTests.cpp
)
target_link_libraries(rpg_tests PRIVATE rpg_server Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
target_include_directories(rpg_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(rpg_tests PRIVATE
  RPG_TEST_DATA_DIR="${RPG_DATA_DIR}"
  RPG_TEST_SIM_DIR="${RPG_SIM_DIR}"
  RPG_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/parity/fixtures"
  RPG_TEST_SERVER_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/server/fixtures"
)
rpg_set_warnings(rpg_tests)
if(WIN32)
  # Os nomes dos casos têm acentos; no Windows a página de código do console atrapalha a descoberta.
  add_test(NAME rpg_tests COMMAND rpg_tests)
else()
  catch_discover_tests(rpg_tests)
endif()

# Cliente sem janela (lógica, sessão contra o servidor, textos, geometria): roda em qualquer CI.
if(RPG_BUILD_CLIENT)
  add_executable(rpg_client_tests client/ClientTests.cpp client/ScenePackTests.cpp client/PackSceneTests.cpp client/AnimTests.cpp
    client/AudioTests.cpp client/RenderLogicTests.cpp)
  target_link_libraries(rpg_client_tests PRIVATE rpg_client_core rpg_server Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
  target_include_directories(rpg_client_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_definitions(rpg_client_tests PRIVATE
    RPG_TEST_DATA_DIR="${RPG_DATA_DIR}"
    RPG_TEST_SIM_DIR="${RPG_SIM_DIR}"
    RPG_TEST_ASSETS_DIR="${RPG_ASSETS_DIR}"
    RPG_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/parity/fixtures"
  )
  rpg_set_warnings(rpg_client_tests)
  if(WIN32)
    add_test(NAME rpg_client_tests COMMAND rpg_client_tests)
  else()
    catch_discover_tests(rpg_client_tests)
  endif()
  # Jogo inteiro sem janela (Vulkan; em CI, mesa lavapipe): entra no mundo, joga 2 s e grava uma captura.
  option(RPG_TEST_GPU "Registra os testes que precisam de GPU/Vulkan (rpg_local sem janela)" OFF)
  if(RPG_TEST_GPU)
    add_test(NAME smoke.client_headless
      COMMAND rpg_local --headless --auto --size 640x360 --frames 60 --screenshot "${CMAKE_BINARY_DIR}/smoke_client.png")
    add_test(NAME smoke.client_view_title
      COMMAND rpg_local --headless --size 640x360 --view 01_title_screen --frames 10 --screenshot "${CMAKE_BINARY_DIR}/smoke_title.png")
    # personagem de perto: malhas com pele (skinned.vert), arma na mão e NPCs
    add_test(NAME smoke.client_portrait
      COMMAND rpg_local --headless --auto --dev --hide-hud --portrait --size 640x360 --frames 40 --screenshot "${CMAKE_BINARY_DIR}/smoke_portrait.png")
    # sombras, oclusão, bloom, FXAA e cobertura do chão nos três níveis (tools/measure-benchmark.mjs)
    add_test(NAME smoke.client_benchmark
      COMMAND rpg_local --benchmark --size 640x360 --bench-frames 2 --bench-out "${CMAKE_BINARY_DIR}/benchmark-cpp.json")
  endif()
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
add_test(NAME smoke.server_headless COMMAND rpg_server_app --data "${RPG_DATA_DIR}" --sim "${RPG_SIM_DIR}" --ticks 300)
# Dois minutos de jogo com oito bots: combate, mortes, reaparecimento, grupos, acampamento e evento.
add_test(NAME smoke.server_bots COMMAND rpg_server_app --data "${RPG_DATA_DIR}" --sim "${RPG_SIM_DIR}" --ticks 3600 --bots 8)
