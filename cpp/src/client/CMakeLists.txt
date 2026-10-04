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
  game/Settings.cpp
  game/Quality.cpp
  game/ShadowCamera.cpp
  fx/FxState.cpp
  audio/Synth.cpp
  ui/Localization.cpp
  ui/FontAtlas.cpp
  ui/UiBatch.cpp
  ui/Hud.cpp
  ui/GameUi.cpp
  ui/GameHud.cpp
  ui/WorldMap.cpp
  ui/BookText.cpp
  ui/GamePanels.cpp
  ui/GameBook.cpp
  ui/RmlRender.cpp
  ui/UiSystem.cpp
  ui/UiDecorators.cpp
  geom/Primitives.cpp
  geom/TerrainMesh.cpp
  geom/SceneBatch.cpp
  image/PngWriter.cpp
  assets/ScenePack.cpp
  assets/Image.cpp
  assets/EnvironmentMap.cpp
  world/PackScene.cpp
  world/GroundCover.cpp
  world/PackMaterials.cpp
  world/SkyView.cpp
  world/EntityModels.cpp
  anim/Rig.cpp
  anim/Mixer.cpp
  anim/HumanoidAnimator.cpp
  anim/QuadrupedAnimator.cpp
  entities/CharacterViews.cpp
)
target_link_libraries(rpg_client_core
  PUBLIC rpg_core rpg_net Freetype::Freetype rpg_rmlui
  PRIVATE nlohmann_json::nlohmann_json rpg_meshopt rpg_webp
)
rpg_set_warnings(rpg_client_core)

add_library(rpg_client STATIC
  platform/Platform.cpp
  audio/AudioDevice.cpp
  render/Renderer.cpp
  render/RendererPack.cpp
  render/RendererRml.cpp
  render/RendererPost.cpp
  app/ClientApp.cpp
)
if(TARGET rpg_imgui AND RPG_FETCH_DEPS)
  target_sources(rpg_client PRIVATE app/DebugTools.cpp)
endif()
target_link_libraries(rpg_client PUBLIC rpg_client_core rpg_shaders SDL3::SDL3 rpg_imgui)
rpg_set_warnings(rpg_client)
