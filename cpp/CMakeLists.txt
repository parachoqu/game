cmake_minimum_required(VERSION 3.28)

# Reescrita em C++20 da demo (demo/src). A arquitetura está em ARQUITETURA.md:
#   rpg_core   — compartilhado: dados de design, matemática, Rng, relógio, contêineres
#   rpg_net    — transporte (LocalTransport agora; rede de verdade na fase online)
#   rpg_sim    — simulação autoritativa: só depende de rpg_core
#   rpg_server — host da simulação: tick fixo, sessões, persistência
#   rpg_client — janela, render, input e UI (fase 3; nunca linka rpg_sim)
project(ProjetoGame VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# O projeto não usa módulos C++20. Com C++20 + Ninja, a política CMP0155 faria o CMake pedir ao
# compilador um escaneamento de módulos (`-fmodules-ts -fdeps-format=p1689r5` no GCC ≥ 14). O mapeador
# de módulos do GCC quebra em caminhos com espaço e acento ("Área de trabalho"): "cannot determine
# '#include' translation … unrecognized 'INCLUDE-TRANSLATE". Desligado, o build não depende do caminho.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(RPG_BUILD_TESTS "Compila os testes (Catch2)" ON)
option(RPG_WARNINGS_AS_ERRORS "Trata avisos como erros" OFF)
option(RPG_SANITIZE "Compila com AddressSanitizer + UBSan" OFF)

# Determinismo numérico: sem fusão de multiplicação+soma (FMA) nem matemática "rápida". Servidor e
# cliente precisam chegar ao mesmo resultado em qualquer CPU, e os testes comparam com o JS bit a bit.
if(MSVC)
  # /utf-8: fontes e literais em UTF-8 (textos em pt-BR e caminhos com acento), qualquer que seja a
  # página de código do Windows.
  add_compile_options(/fp:precise /utf-8)
else()
  add_compile_options(-ffp-contract=off -fno-fast-math)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)
include(Sanitizers)
include(ClientDeps)
include(NetDeps)

find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json 3.11 CONFIG REQUIRED)

# Diretório de dados padrão para rodar direto da árvore de fontes (os executáveis aceitam --data).
set(RPG_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data" CACHE PATH "Diretório de cpp/data")
set(RPG_SIM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/sim" CACHE PATH "Diretório do pacote de mundo da simulação")
set(RPG_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets" CACHE PATH "Diretório de assets do cliente (fontes…)")
set(RPG_DEMO_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../demo/assets" CACHE PATH "Assets da demo (modelos, texturas; Git LFS)")

add_subdirectory(src/core)
add_subdirectory(src/net)
add_subdirectory(src/sim)
add_subdirectory(src/server)
add_subdirectory(apps/server)
if(RPG_BUILD_CLIENT)
  add_subdirectory(shaders)
  add_subdirectory(src/client)
  add_subdirectory(apps/local)
endif()

if(RPG_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
