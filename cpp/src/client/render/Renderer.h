#pragma once
// Renderizador SDL_GPU (backend Vulkan com SPIR-V). Passes por quadro:
//   1. cena em HDR (RGBA16F + profundidade): céu → relevo → cenário e entidades instanciados →
//      água → efeitos sem luz;
//   2. saída (RGBA8): composição (ACES + sRGB) e interface 2D;
//   3. cópia da saída para a janela e, quando pedida, para a memória (captura de tela).
// A cena é descrita pelo app a cada quadro (listas de instâncias, vértices sem luz, vértices da UI);
// o relevo e o cenário estático de cada mapa sobem para a GPU uma vez.
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>

#include "client/assets/EnvironmentMap.h"
#include "client/assets/ScenePack.h"
#include "client/geom/SceneBatch.h"
#include "client/geom/TerrainMesh.h"
#include "client/render/FrameUniforms.h"
#include "client/ui/RmlRender.h"
#include "client/ui/UiBatch.h"
#include "client/world/PackScene.h"
#include "client/world/SkyView.h"

struct SDL_Window;

namespace rpg::client {

struct RendererOptions {
  int width = 1280, height = 720;  // tamanho da saída sem janela
  bool debug = false;
  bool vsync = true;
};

// Qualidade (engine/quality.js): mapa de sombra do sol e passes de pós-processamento.
struct RenderSettings {
  int shadowSize = 4096;  // lado do mapa de sombra; 0 = sem sombra
  bool gtao = true, bloom = true, fxaa = true;
  bool operator==(const RenderSettings&) const = default;
};

struct FrameStats {
  std::uint32_t shadowDraws = 0;
  std::uint32_t drawCalls = 0;
  std::uint64_t triangles = 0;
  std::uint32_t chunksDrawn = 0;
  std::uint32_t instances = 0;
};

class Renderer {
 public:
  Renderer(SDL_Window* window, const RendererOptions& o);
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  // Relevo, água, pontes e cenário estático de um mapa (índice = MapKind). Cenário grey-box: só é
  // desenhado enquanto o pacote visual não estiver carregado.
  void uploadMap(int map, const TerrainBuild& terrain, const InstanceLists& props);

  // Pacote visual (cpp/assets/client): texturas, malhas e materiais. `progress` recebe 0–1.
  void loadPack(const ScenePack& pack, const PackMeshes& meshes, const std::vector<Instance>& staticInstances,
                const std::function<void(double)>& progress = {});
  bool packLoaded() const;
  // Luz de ambiente (equirretangular linear); nullptr remove.
  void setEnvironment(const EnvironmentMap* env);

  void configure(const RenderSettings& s);
  void waitIdle();  // espera a GPU terminar (medidas de tempo de quadro)
  const RenderSettings& settings() const;

  struct Frame {
    FrameUniforms uniforms;
    int map = 0;
    glm::vec3 cameraPos{0.0f};
    const InstanceLists* dynamic = nullptr;
    const PackFrame* pack = nullptr;  // cenário do pacote visual (com o pacote carregado)
    // Passada de sombra do sol (PackFrame::shadowDraws), com `uniforms.shadowMatrix` e `uniforms.shadow`
    // já preenchidos; sem ela, `uniforms.shadow[3]` precisa ser 0.
    bool shadows = false;
    glm::mat4 view{1.0f}, proj{1.0f};
    // Pausa barata: `cacheScene` guarda a cena composta deste quadro; `reuseScene` a reaproveita (sem
    // sombra, cena nem pós) e só redesenha a interface por cima. Sem cache válido, desenha tudo.
    bool cacheScene = false, reuseScene = false;  // câmera (a oclusão de ambiente trabalha no espaço de vista)
    const SkyFrame* sky = nullptr;    // estrelas, constelações, lua
    const std::vector<ColorVertex>* unlit = nullptr;
    const UiBatch* ui = nullptr;
    FontAtlas* atlas = nullptr;
    const RmlRender* rml = nullptr;  // interface RmlUi (por cima de tudo)
    // Desenho extra por cima de tudo (ferramentas de depuração): recebe o buffer de comandos e a
    // textura de saída, fora de qualquer passe.
    std::function<void(SDL_GPUCommandBuffer*, SDL_GPUTexture*)> overlay;
    float exposure = 1.0f;
  };
  // Desenha um quadro. `capture`: grava a saída em PNG (espera a GPU terminar).
  bool render(const Frame& f, const std::optional<std::filesystem::path>& capture = std::nullopt);

  int width() const { return outW_; }
  int height() const { return outH_; }
  const FrameStats& stats() const { return stats_; }
  const char* driver() const;
  SDL_GPUDevice* device() const;
  static SDL_GPUTextureFormat outputFormat();  // formato da textura de saída (o que o overlay desenha)

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  int outW_ = 0, outH_ = 0;
  FrameStats stats_;
};

}  // namespace rpg::client
