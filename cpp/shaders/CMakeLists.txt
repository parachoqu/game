# Shaders do cliente: GLSL (shaders/src) → SPIR-V → embutidos no executável (rpg_shaders).
#
# Com glslangValidator, o SPIR-V é gerado no build a partir do GLSL. Sem ele, valem os .spv
# versionados em shaders/spv. Depois de mudar um shader:
#   cmake --build --preset dev --target rpg_update_shaders
# copia os .spv recém-compilados para shaders/spv (commite junto com o GLSL).
set(RPG_SHADERS
  mesh.vert surface.frag terrain.frag
  scene.vert scene.frag water.frag
  unlit.vert unlit.frag
  fullscreen.vert sky.frag composite.frag stars.vert stars.frag
  ui.vert ui.frag
)
set(_src "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(_committed "${CMAKE_CURRENT_SOURCE_DIR}/spv")
set(_out "${CMAKE_CURRENT_BINARY_DIR}/spv")
file(MAKE_DIRECTORY "${_out}")

set(_spv_files)
if(GLSLANG_VALIDATOR)
  file(GLOB _includes "${_src}/*.glsl")
  foreach(s IN LISTS RPG_SHADERS)
    add_custom_command(
      OUTPUT "${_out}/${s}.spv"
      COMMAND "${GLSLANG_VALIDATOR}" -V --target-env vulkan1.0 --quiet -o "${_out}/${s}.spv" "${_src}/${s}"
      DEPENDS "${_src}/${s}" ${_includes}
      COMMENT "GLSL → SPIR-V: ${s}"
      VERBATIM)
    list(APPEND _spv_files "${_out}/${s}.spv")
  endforeach()
  add_custom_target(rpg_update_shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_spv_files} "${_committed}"
    DEPENDS ${_spv_files}
    COMMENT "Atualizando shaders/spv")
else()
  message(STATUS "glslangValidator não encontrado: usando os SPIR-V versionados em shaders/spv")
  foreach(s IN LISTS RPG_SHADERS)
    list(APPEND _spv_files "${_committed}/${s}.spv")
  endforeach()
endif()

set(_embedded "${CMAKE_CURRENT_BINARY_DIR}/rpg_shaders.cpp")
add_custom_command(
  OUTPUT "${_embedded}"
  COMMAND ${CMAKE_COMMAND} -DOUT=${_embedded} "-DFILES=${_spv_files}" -P "${PROJECT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
  DEPENDS ${_spv_files} "${PROJECT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
  COMMENT "Embutindo shaders"
  VERBATIM)

add_library(rpg_shaders STATIC "${_embedded}")
target_include_directories(rpg_shaders PUBLIC "${PROJECT_SOURCE_DIR}/src")
