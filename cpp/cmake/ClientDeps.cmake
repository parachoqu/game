# Dependências do cliente (janela, GPU, texto). O servidor e os testes de simulação não usam nada disto.
#
# Procura primeiro o que está instalado (apt/dnf/brew/vcpkg). O que faltar é baixado por git com tag
# fixa (RPG_FETCH_DEPS), para o build funcionar em distros sem SDL3 empacotada (Ubuntu 24.04).
option(RPG_BUILD_CLIENT "Compila o cliente (SDL3 + SDL_GPU) e o jogo local" ON)
option(RPG_FETCH_DEPS "Baixa por git as dependências do cliente que não estiverem instaladas" ON)

if(NOT RPG_BUILD_CLIENT)
  return()
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# ---------------------------------------------------------------- SDL3
find_package(SDL3 3.2 CONFIG QUIET)
if(SDL3_FOUND)
  message(STATUS "SDL3 do sistema: ${SDL3_VERSION}")
elseif(RPG_FETCH_DEPS)
  message(STATUS "SDL3 não encontrada: baixando release-3.4.16 por git")
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-3.4.16 GIT_SHALLOW TRUE SYSTEM)
  FetchContent_MakeAvailable(SDL3)
else()
  message(WARNING "SDL3 não encontrada e RPG_FETCH_DEPS=OFF: o cliente não será compilado")
  set(RPG_BUILD_CLIENT OFF)
  return()
endif()

# ---------------------------------------------------------------- FreeType (texto do HUD e da interface)
find_package(Freetype QUIET)
if(NOT Freetype_FOUND)
  if(RPG_FETCH_DEPS)
    message(STATUS "FreeType não encontrada: baixando VER-2-13-3 por git")
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    FetchContent_Declare(freetype GIT_REPOSITORY https://github.com/freetype/freetype.git GIT_TAG VER-2-13-3 GIT_SHALLOW TRUE SYSTEM)
    FetchContent_MakeAvailable(freetype)
    add_library(Freetype::Freetype ALIAS freetype)
  else()
    message(WARNING "FreeType não encontrada e RPG_FETCH_DEPS=OFF: o cliente não será compilado")
    set(RPG_BUILD_CLIENT OFF)
    return()
  endif()
endif()

# ---------------------------------------------------------------- shaders (GLSL → SPIR-V)
# Com glslangValidator instalado, os shaders são compilados no build; sem ele, valem os .spv
# versionados em shaders/spv (atualize-os com `cmake --build <dir> --target rpg_update_shaders`).
find_program(GLSLANG_VALIDATOR glslangValidator)
