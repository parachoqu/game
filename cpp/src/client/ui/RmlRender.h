#pragma once
// Interface de desenho do RmlUi que só grava (sem SDL; testável). A cada quadro o RmlUi descreve a
// interface em comandos — geometria, recorte, transformação, máscara de recorte, camadas, filtros,
// degradês e camadas salvas como textura (sombras de caixa) — e o Renderer os executa na mesma ordem,
// no fim da saída, por cima da cena. Geometrias e texturas ficam guardadas na CPU até serem soltas;
// o Renderer mantém as cópias da GPU pelo identificador.
//
// Os identificadores nunca se repetem: o que o RmlUi solta num quadro só some depois que o Renderer
// executou esse quadro (endFrame).
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <RmlUi/Core/RenderInterface.h>
#include <glm/glm.hpp>

namespace rpg::client {

// Mesmo layout de Rml::Vertex: posição em pixels, cor pré-multiplicada, coordenada de textura.
struct RmlVertex {
  float x, y;
  std::uint8_t rgba[4];
  float u, v;
};
static_assert(sizeof(RmlVertex) == 20);

struct RmlGeometry {
  std::vector<RmlVertex> vertices;
  std::vector<std::uint32_t> indices;
};

struct RmlTexture {
  int w = 0, h = 0;
  std::vector<std::uint8_t> rgba;  // pré-multiplicado; vazio nas camadas salvas (a GPU preenche)
  std::uint32_t version = 1;       // muda quando os pixels mudam (texturas dinâmicas)
  bool renderTarget = false;       // SaveLayerAsTexture
};

inline constexpr int kRmlMaxStops = 16;

// Degradês do RmlUi 6 (linear, radial, cônico e as versões repetidas): mesmos parâmetros do
// renderizador GL3 de referência.
struct RmlGradient {
  int func = 0;  // 0 linear, 1 radial, 2 cônico, 3–5 repetidos
  glm::vec2 p{0.0f}, v{0.0f};
  int stops = 0;
  std::array<float, kRmlMaxStops> positions{};
  std::array<glm::vec4, kRmlMaxStops> colors{};  // pré-multiplicadas
};

struct RmlFilter {
  enum class Type : std::uint8_t { Opacity, Blur, DropShadow, ColorMatrix, MaskImage } type = Type::Opacity;
  float value = 1;  // opacidade
  float sigma = 0;  // desfoque
  glm::vec2 offset{0.0f};
  glm::vec4 color{0.0f};  // pré-multiplicada
  glm::mat4 colorMatrix{1.0f};
};

enum class RmlOp : std::uint8_t {
  Draw,         // geometria (com ou sem textura)
  Shader,       // geometria com degradê
  Scissor,      // recorte (enabled = false: sem recorte)
  Transform,    // transformação (index = -1: nenhuma)
  ClipEnable,   // máscara de recorte ligada/desligada
  ClipMask,     // escreve na máscara (clipOp: 0 Set, 1 SetInverse, 2 Intersect)
  PushLayer,    // nova camada (limpa)
  PopLayer,
  Composite,    // camada `layer` → `dstLayer`, com filtros e mistura (0 mistura, 1 substitui)
  SaveTexture,  // camada do topo, no recorte atual, vira a textura `texture`
  SaveMask,     // camada do topo vira a máscara do filtro `filter`
};

struct RmlCmd {
  RmlOp op = RmlOp::Draw;
  std::uint64_t geometry = 0, texture = 0, shader = 0, filter = 0;
  glm::vec2 translation{0.0f};
  bool enabled = false;
  std::array<int, 4> rect{};  // x0, y0, x1, y1
  int index = -1;             // Transform
  int clipOp = 0;
  int layer = 0, dstLayer = 0, blend = 0;
  int filterFirst = 0, filterCount = 0;  // em RmlRender::filterRefs()
};

class RmlRender final : public Rml::RenderInterface {
 public:
  // Carrega as imagens pedidas pelos documentos (<img src>, decoradores): devolve false se não achou.
  using Loader = std::function<bool(const std::string& source, RmlTexture& out)>;

  RmlRender();
  ~RmlRender() override;
  void setLoader(Loader l) { loader_ = std::move(l); }

  // Antes de Context::Render(): tamanho da saída e comandos zerados.
  void beginFrame(int width, int height);
  // Depois que o Renderer executou o quadro: solta o que o RmlUi liberou.
  void endFrame();

  // Texturas dinâmicas (minimapa, mapa completo): `source` é o que os documentos usam em src.
  void setDynamicTexture(const std::string& source, int w, int h, std::vector<std::uint8_t> rgbaPremultiplied);

  // ---- o que o Renderer lê
  int width() const { return width_; }
  int height() const { return height_; }
  const std::vector<RmlCmd>& commands() const { return cmds_; }
  const RmlGeometry* geometry(std::uint64_t h) const;
  const RmlTexture* texture(std::uint64_t h) const;
  const RmlGradient* gradient(std::uint64_t h) const;
  const RmlFilter* filter(std::uint64_t h) const;
  const std::vector<std::uint64_t>& filterRefs() const { return filterRefs_; }
  const std::vector<glm::mat4>& transforms() const { return transforms_; }
  // Texturas soltas desde o último quadro (o Renderer libera as cópias da GPU).
  const std::vector<std::uint64_t>& releasedTextures() const { return releasedTextures_; }

  // ---- Rml::RenderInterface
  Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
  void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
  void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
  Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override;
  Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override;
  void ReleaseTexture(Rml::TextureHandle texture) override;
  void EnableScissorRegion(bool enable) override;
  void SetScissorRegion(Rml::Rectanglei region) override;
  void EnableClipMask(bool enable) override;
  void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) override;
  void SetTransform(const Rml::Matrix4f* transform) override;
  Rml::LayerHandle PushLayer() override;
  void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode mode,
                       Rml::Span<const Rml::CompiledFilterHandle> filters) override;
  void PopLayer() override;
  Rml::TextureHandle SaveLayerAsTexture() override;
  Rml::CompiledFilterHandle SaveLayerAsMaskImage() override;
  Rml::CompiledFilterHandle CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters) override;
  void ReleaseFilter(Rml::CompiledFilterHandle filter) override;
  Rml::CompiledShaderHandle CompileShader(const Rml::String& name, const Rml::Dictionary& parameters) override;
  void RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                    Rml::TextureHandle texture) override;
  void ReleaseShader(Rml::CompiledShaderHandle shader) override;

 private:
  std::uint64_t next() { return ++nextId_; }

  Loader loader_;
  int width_ = 1, height_ = 1;
  std::uint64_t nextId_ = 0;
  std::unordered_map<std::uint64_t, RmlGeometry> geometries_;
  std::unordered_map<std::uint64_t, RmlTexture> textures_;
  std::unordered_map<std::uint64_t, RmlGradient> gradients_;
  std::unordered_map<std::uint64_t, RmlFilter> filters_;
  std::map<std::string, std::uint64_t> dynamic_;  // fonte → textura
  std::vector<RmlCmd> cmds_;
  std::vector<std::uint64_t> filterRefs_;
  std::vector<glm::mat4> transforms_;
  std::vector<std::uint64_t> deadGeometry_, deadTextures_, deadGradients_, deadFilters_, releasedTextures_;
  std::array<int, 4> scissor_{};
  bool scissorOn_ = false;
  int layers_ = 1;
};

}  // namespace rpg::client
