# Cliente em duas bibliotecas, ambas sem a simulação:
#   rpg_client_core — lógica sem plataforma (sessão, interpolação, câmera, mira, alvos, textos, HUD,
#                     geometria): testável sem janela nem GPU;
#   rpg_client      — SDL3 + SDL_GPU (janela, input, renderizador) e o app.
add_library(rpg_client_core STATIC
  ClientWorld.cpp
  Presentation.cpp
  net/SnapshotInterpolator.cpp
  net/ClientSession.cpp
  game/ViewCamera.cpp
  game/ThirdPersonCamera.cpp
  game/Targets.cpp
  game/Aim.cpp
  game/CommandBuilder.cpp
  game/Lighting.cpp
  fx/FxState.cpp
  ui/Localization.cpp
  ui/FontAtlas.cpp
  ui/UiBatch.cpp
  ui/Hud.cpp
  ui/Screens.cpp
  geom/Primitives.cpp
  geom/TerrainMesh.cpp
  geom/SceneBatch.cpp
  image/PngWriter.cpp
)
target_link_libraries(rpg_client_core
  PUBLIC rpg_core rpg_net Freetype::Freetype
  PRIVATE nlohmann_json::nlohmann_json
)
rpg_set_warnings(rpg_client_core)

add_library(rpg_client STATIC
  platform/Platform.cpp
  render/Renderer.cpp
  app/ClientApp.cpp
)
target_link_libraries(rpg_client PUBLIC rpg_client_core rpg_shaders SDL3::SDL3)
rpg_set_warnings(rpg_client)
