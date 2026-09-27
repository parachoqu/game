cmake_minimum_required(VERSION 3.28)

# Reescrita em C++20 da demo (demo/src). A arquitetura está em ARQUITETURA.md:
#   rpg_core   — compartilhado: dados de design, matemática, Rng, relógio, contêineres
#   rpg_net    — transporte (LocalTransport agora; rede de verdade na fase online)
#   rpg_sim    — simulação autoritativa: só depende de rpg_core
#   rpg_server — host da simulação: tick fixo, sessões, persistência
#   rpg_client — janela, render, input e UI (fase 3; nunca linka rpg_sim)
project(ProjetoGame VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RPG_BUILD_TESTS "Compila os testes (Catch2)" ON)
option(RPG_WARNINGS_AS_ERRORS "Trata avisos como erros" OFF)
option(RPG_SANITIZE "Compila com AddressSanitizer + UBSan" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)
include(Sanitizers)

find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json 3.11 CONFIG REQUIRED)

# Diretório de dados padrão para rodar direto da árvore de fontes (os executáveis aceitam --data).
set(RPG_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data" CACHE PATH "Diretório de cpp/data")

add_subdirectory(src/core)
add_subdirectory(src/net)
add_subdirectory(src/sim)
add_subdirectory(src/server)
add_subdirectory(apps/server)

if(RPG_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
