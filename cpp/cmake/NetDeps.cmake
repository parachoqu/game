# ENet (rede da fase 8): biblioteca C pequena, com canais confiáveis e não confiáveis sobre UDP.
#
# Ordem: o port do vcpkg (unofficial-enet), o pacote do sistema (pkg-config libenet: libenet-dev no
# Debian/Ubuntu/Kali, enet-devel no Fedora) e, sem nenhum dos dois, a tag v1.3.18 por git. O resultado
# é sempre o alvo `rpg::enet`.
option(RPG_FETCH_DEPS "Baixa por git as dependências que não estiverem instaladas" ON)

find_package(unofficial-enet CONFIG QUIET)
if(TARGET unofficial::enet::enet)
  message(STATUS "ENet do vcpkg")
  add_library(rpg::enet ALIAS unofficial::enet::enet)
  return()
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(ENET QUIET IMPORTED_TARGET libenet)
  if(ENET_FOUND)
    message(STATUS "ENet do sistema: ${ENET_VERSION}")
    add_library(rpg::enet ALIAS PkgConfig::ENET)
    return()
  endif()
endif()

if(NOT RPG_FETCH_DEPS)
  message(FATAL_ERROR "ENet não encontrada (instale libenet-dev ou use o vcpkg) e RPG_FETCH_DEPS=OFF")
endif()
message(STATUS "ENet não encontrada: baixando v1.3.18 por git")
include(FetchContent)
# SOURCE_SUBDIR sem CMakeLists: só baixa. As fontes entram direto (o CMakeLists do ENet é antigo e não
# exporta os headers no alvo).
FetchContent_Declare(enet GIT_REPOSITORY https://github.com/lsalzman/enet.git GIT_TAG v1.3.18 GIT_SHALLOW TRUE
                     SOURCE_SUBDIR rpg-sem-cmake)
FetchContent_MakeAvailable(enet)
file(GLOB _enet_sources CONFIGURE_DEPENDS "${enet_SOURCE_DIR}/*.c")
add_library(rpg_enet_vendored STATIC ${_enet_sources})
target_include_directories(rpg_enet_vendored SYSTEM PUBLIC "${enet_SOURCE_DIR}/include")
if(WIN32)
  target_link_libraries(rpg_enet_vendored PUBLIC ws2_32 winmm)
else()
  target_compile_definitions(rpg_enet_vendored PRIVATE HAS_SOCKLEN_T=1 HAS_POLL=1 HAS_FCNTL=1 HAS_INET_PTON=1 HAS_INET_NTOP=1
                                                       HAS_MSGHDR_FLAGS=1 HAS_GETADDRINFO=1 HAS_GETNAMEINFO=1)
endif()
set_target_properties(rpg_enet_vendored PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(rpg::enet ALIAS rpg_enet_vendored)
