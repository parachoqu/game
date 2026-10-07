#include "client/ui/RmlRender.h"

#include <algorithm>
#include <cstring>

#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/Dictionary.h>
#include <RmlUi/Core/Log.h>

#include "core/Log.h"

namespace rpg::client {

namespace {

glm::vec4 colorf(Rml::ColourbPremultiplied c) {
  return glm::vec4(c.red, c.green, c.blue, c.alpha) * (1.0f / 255.0f);
}

template <class T>
T param(const Rml::Dictionary& d, const char* key, T fallback) {
  return Rml::Get(d, key, fallback);
}

}  // namespace

RmlRender::RmlRender() = default;
RmlRender::~RmlRender() = default;

void RmlRender::beginFrame(int width, int height) {
  width_ = std::max(1, width);
  height_ = std::max(1, height);
  cmds_.clear();
  filterRefs_.clear();
  transforms_.clear();
  scissorOn_ = false;
  layers_ = 1;
}

void RmlRender::endFrame() {
  for (std::uint64_t h : deadGeometry_) geometries_.erase(h);
  for (std::uint64_t h : deadGradients_) gradients_.erase(h);
  for (std::uint64_t h : deadFilters_) filters_.erase(h);
  for (std::uint64_t h : deadTextures_) textures_.erase(h);
  releasedTextures_ = deadTextures_;
  deadGeometry_.clear();
  deadGradients_.clear();
  deadFilters_.clear();
  deadTextures_.clear();
}

void RmlRender::setDynamicTexture(const std::string& source, int w, int h, std::vector<std::uint8_t> rgba) {
  auto it = dynamic_.find(source);
  if (it == dynamic_.end()) it = dynamic_.emplace(source, next()).first;
  RmlTexture& t = textures_[it->second];
  const std::uint32_t version = t.version + 1;
  t.w = w;
  t.h = h;
  t.rgba = std::move(rgba);
  t.version = version;
}

const RmlGeometry* RmlRender::geometry(std::uint64_t h) const {
  const auto it = geometries_.find(h);
  return it == geometries_.end() ? nullptr : &it->second;
}
const RmlTexture* RmlRender::texture(std::uint64_t h) const {
  const auto it = textures_.find(h);
  return it == textures_.end() ? nullptr : &it->second;
}
const RmlGradient* RmlRender::gradient(std::uint64_t h) const {
  const auto it = gradients_.find(h);
  return it == gradients_.end() ? nullptr : &it->second;
}
const RmlFilter* RmlRender::filter(std::uint64_t h) const {
  const auto it = filters_.find(h);
  return it == filters_.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------- geometria e texturas

Rml::CompiledGeometryHandle RmlRender::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) {
  static_assert(sizeof(Rml::Vertex) == sizeof(RmlVertex));
  const std::uint64_t h = next();
  RmlGeometry& g = geometries_[h];
  g.vertices.resize(vertices.size());
  if (!vertices.empty()) std::memcpy(g.vertices.data(), vertices.data(), vertices.size() * sizeof(RmlVertex));
  g.indices.resize(indices.size());
  for (std::size_t i = 0; i < indices.size(); ++i) g.indices[i] = static_cast<std::uint32_t>(indices[i]);
  return static_cast<Rml::CompiledGeometryHandle>(h);
}

void RmlRender::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) {
  RmlCmd c;
  c.op = RmlOp::Draw;
  c.geometry = static_cast<std::uint64_t>(geometry);
  c.texture = static_cast<std::uint64_t>(texture);
  c.translation = {translation.x, translation.y};
  cmds_.push_back(c);
}

void RmlRender::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) { deadGeometry_.push_back(static_cast<std::uint64_t>(geometry)); }

Rml::TextureHandle RmlRender::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) {
  if (const auto it = dynamic_.find(source); it != dynamic_.end()) {
    const RmlTexture& t = textures_[it->second];
    dimensions = {t.w, t.h};
    return static_cast<Rml::TextureHandle>(it->second);
  }
  RmlTexture t;
  if (!loader_ || !loader_(source, t) || t.w <= 0 || t.h <= 0) {
    Rml::Log::Message(Rml::Log::LT_WARNING, "textura não encontrada: %s", source.c_str());
    return {};
  }
  dimensions = {t.w, t.h};
  const std::uint64_t h = next();
  textures_.emplace(h, std::move(t));
  return static_cast<Rml::TextureHandle>(h);
}

Rml::TextureHandle RmlRender::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) {
  RmlTexture t;
  t.w = dimensions.x;
  t.h = dimensions.y;
  t.rgba.assign(source.begin(), source.end());
  const std::uint64_t h = next();
  textures_.emplace(h, std::move(t));
  return static_cast<Rml::TextureHandle>(h);
}

void RmlRender::ReleaseTexture(Rml::TextureHandle texture) {
  const auto h = static_cast<std::uint64_t>(texture);
  for (auto it = dynamic_.begin(); it != dynamic_.end(); ++it)
    if (it->second == h) {
      dynamic_.erase(it);
      break;
    }
  deadTextures_.push_back(h);
}

// ---------------------------------------------------------------- estado

void RmlRender::EnableScissorRegion(bool enable) {
  scissorOn_ = enable;
  RmlCmd c;
  c.op = RmlOp::Scissor;
  c.enabled = enable;
  c.rect = scissor_;
  cmds_.push_back(c);
}

void RmlRender::SetScissorRegion(Rml::Rectanglei region) {
  scissor_ = {region.Left(), region.Top(), region.Right(), region.Bottom()};
  RmlCmd c;
  c.op = RmlOp::Scissor;
  c.enabled = scissorOn_ = true;
  c.rect = scissor_;
  cmds_.push_back(c);
}

void RmlRender::EnableClipMask(bool enable) {
  RmlCmd c;
  c.op = RmlOp::ClipEnable;
  c.enabled = enable;
  cmds_.push_back(c);
}

void RmlRender::RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) {
  RmlCmd c;
  c.op = RmlOp::ClipMask;
  c.clipOp = operation == Rml::ClipMaskOperation::Set ? 0 : operation == Rml::ClipMaskOperation::SetInverse ? 1 : 2;
  c.geometry = static_cast<std::uint64_t>(geometry);
  c.translation = {translation.x, translation.y};
  cmds_.push_back(c);
}

void RmlRender::SetTransform(const Rml::Matrix4f* transform) {
  RmlCmd c;
  c.op = RmlOp::Transform;
  if (transform) {
    glm::mat4 m;
    // Rml::Matrix4f é coluna a coluna por padrão (RMLUI_MATRIX_ROW_MAJOR desligado), como o glm
    std::memcpy(&m[0][0], transform->data(), sizeof(float) * 16);
    c.index = static_cast<int>(transforms_.size());
    transforms_.push_back(m);
  }
  cmds_.push_back(c);
}

// ---------------------------------------------------------------- camadas e filtros

Rml::LayerHandle RmlRender::PushLayer() {
  RmlCmd c;
  c.op = RmlOp::PushLayer;
  c.layer = layers_++;
  cmds_.push_back(c);
  return static_cast<Rml::LayerHandle>(c.layer);
}

void RmlRender::CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode mode,
                                Rml::Span<const Rml::CompiledFilterHandle> filters) {
  RmlCmd c;
  c.op = RmlOp::Composite;
  c.layer = static_cast<int>(source);
  c.dstLayer = static_cast<int>(destination);
  c.blend = mode == Rml::BlendMode::Replace ? 1 : 0;
  c.filterFirst = static_cast<int>(filterRefs_.size());
  for (Rml::CompiledFilterHandle f : filters) filterRefs_.push_back(static_cast<std::uint64_t>(f));
  c.filterCount = static_cast<int>(filters.size());
  cmds_.push_back(c);
}

void RmlRender::PopLayer() {
  RmlCmd c;
  c.op = RmlOp::PopLayer;
  c.layer = --layers_;
  cmds_.push_back(c);
}

Rml::TextureHandle RmlRender::SaveLayerAsTexture() {
  RmlTexture t;
  t.w = std::max(1, scissor_[2] - scissor_[0]);
  t.h = std::max(1, scissor_[3] - scissor_[1]);
  t.renderTarget = true;
  const std::uint64_t h = next();
  textures_.emplace(h, std::move(t));
  RmlCmd c;
  c.op = RmlOp::SaveTexture;
  c.texture = h;
  c.rect = scissor_;
  cmds_.push_back(c);
  return static_cast<Rml::TextureHandle>(h);
}

Rml::CompiledFilterHandle RmlRender::SaveLayerAsMaskImage() {
  RmlFilter f;
  f.type = RmlFilter::Type::MaskImage;
  const std::uint64_t h = next();
  filters_.emplace(h, f);
  RmlCmd c;
  c.op = RmlOp::SaveMask;
  c.filter = h;
  cmds_.push_back(c);
  return static_cast<Rml::CompiledFilterHandle>(h);
}

Rml::CompiledFilterHandle RmlRender::CompileFilter(const Rml::String& name, const Rml::Dictionary& p) {
  RmlFilter f;
  if (name == "opacity") {
    f.type = RmlFilter::Type::Opacity;
    f.value = param(p, "value", 1.0f);
  } else if (name == "blur") {
    f.type = RmlFilter::Type::Blur;
    f.sigma = param(p, "sigma", 1.0f);
  } else if (name == "drop-shadow") {
    f.type = RmlFilter::Type::DropShadow;
    f.sigma = param(p, "sigma", 0.0f);
    f.color = colorf(param(p, "color", Rml::Colourb()).ToPremultiplied());
    const Rml::Vector2f o = param(p, "offset", Rml::Vector2f(0.0f));
    f.offset = {o.x, o.y};
  } else if (name == "brightness" || name == "contrast" || name == "invert" || name == "grayscale" || name == "sepia" ||
             name == "saturate" || name == "hue-rotate") {
    // matrizes de cor do renderizador GL3 (linhas → glm coluna a coluna)
    f.type = RmlFilter::Type::ColorMatrix;
    const float v = param(p, "value", 1.0f);
    glm::mat4 rows(1.0f);  // rows[r][c] = linha r
    if (name == "brightness") {
      rows = glm::mat4(glm::vec4(v, 0, 0, 0), glm::vec4(0, v, 0, 0), glm::vec4(0, 0, v, 0), glm::vec4(0, 0, 0, 1));
    } else if (name == "contrast") {
      const float g = 0.5f - 0.5f * v;
      rows = glm::mat4(glm::vec4(v, 0, 0, g), glm::vec4(0, v, 0, g), glm::vec4(0, 0, v, g), glm::vec4(0, 0, 0, 1));
    } else if (name == "invert") {
      const float k = std::clamp(v, 0.0f, 1.0f), inv = 1.0f - 2.0f * k;
      rows = glm::mat4(glm::vec4(inv, 0, 0, k), glm::vec4(0, inv, 0, k), glm::vec4(0, 0, inv, k), glm::vec4(0, 0, 0, 1));
    } else if (name == "grayscale") {
      const float r = 1.0f - v;
      const glm::vec3 g = v * glm::vec3(0.2126f, 0.7152f, 0.0722f);
      rows = glm::mat4(glm::vec4(g.x + r, g.y, g.z, 0), glm::vec4(g.x, g.y + r, g.z, 0), glm::vec4(g.x, g.y, g.z + r, 0), glm::vec4(0, 0, 0, 1));
    } else if (name == "sepia") {
      const float r = 1.0f - v;
      const glm::vec3 a = v * glm::vec3(0.393f, 0.769f, 0.189f), b = v * glm::vec3(0.349f, 0.686f, 0.168f), c = v * glm::vec3(0.272f, 0.534f, 0.131f);
      rows = glm::mat4(glm::vec4(a.x + r, a.y, a.z, 0), glm::vec4(b.x, b.y + r, b.z, 0), glm::vec4(c.x, c.y, c.z + r, 0), glm::vec4(0, 0, 0, 1));
    } else if (name == "saturate") {
      rows = glm::mat4(glm::vec4(0.213f + 0.787f * v, 0.715f - 0.715f * v, 0.072f - 0.072f * v, 0),
                       glm::vec4(0.213f - 0.213f * v, 0.715f + 0.285f * v, 0.072f - 0.072f * v, 0),
                       glm::vec4(0.213f - 0.213f * v, 0.715f - 0.715f * v, 0.072f + 0.928f * v, 0), glm::vec4(0, 0, 0, 1));
    } else {
      const float s = std::sin(v), c = std::cos(v);
      rows = glm::mat4(glm::vec4(0.213f + 0.787f * c - 0.213f * s, 0.715f - 0.715f * c - 0.715f * s, 0.072f - 0.072f * c + 0.928f * s, 0),
                       glm::vec4(0.213f - 0.213f * c + 0.143f * s, 0.715f + 0.285f * c + 0.140f * s, 0.072f - 0.072f * c - 0.283f * s, 0),
                       glm::vec4(0.213f - 0.213f * c - 0.787f * s, 0.715f - 0.715f * c + 0.715f * s, 0.072f + 0.928f * c + 0.072f * s, 0),
                       glm::vec4(0, 0, 0, 1));
    }
    f.colorMatrix = glm::transpose(rows);
  } else {
    Rml::Log::Message(Rml::Log::LT_WARNING, "filtro não suportado: %s", name.c_str());
    return {};
  }
  const std::uint64_t h = next();
  filters_.emplace(h, f);
  return static_cast<Rml::CompiledFilterHandle>(h);
}

void RmlRender::ReleaseFilter(Rml::CompiledFilterHandle filter) { deadFilters_.push_back(static_cast<std::uint64_t>(filter)); }

Rml::CompiledShaderHandle RmlRender::CompileShader(const Rml::String& name, const Rml::Dictionary& p) {
  RmlGradient g;
  const bool repeating = param(p, "repeating", false);
  if (name == "linear-gradient") {
    g.func = repeating ? 3 : 0;
    const Rml::Vector2f p0 = param(p, "p0", Rml::Vector2f(0.0f)), p1 = param(p, "p1", Rml::Vector2f(0.0f));
    g.p = {p0.x, p0.y};
    g.v = {p1.x - p0.x, p1.y - p0.y};
  } else if (name == "radial-gradient") {
    g.func = repeating ? 4 : 1;
    const Rml::Vector2f c = param(p, "center", Rml::Vector2f(0.0f)), r = param(p, "radius", Rml::Vector2f(1.0f));
    g.p = {c.x, c.y};
    g.v = {1.0f / r.x, 1.0f / r.y};
  } else if (name == "conic-gradient") {
    g.func = repeating ? 5 : 2;
    const Rml::Vector2f c = param(p, "center", Rml::Vector2f(0.0f));
    const float a = param(p, "angle", 0.0f);
    g.p = {c.x, c.y};
    g.v = {std::cos(a), std::sin(a)};
  } else {
    Rml::Log::Message(Rml::Log::LT_WARNING, "shader não suportado: %s", name.c_str());
    return {};
  }
  const auto it = p.find("color_stop_list");
  if (it != p.end() && it->second.GetType() == Rml::Variant::COLORSTOPLIST) {
    const Rml::ColorStopList& list = it->second.GetReference<Rml::ColorStopList>();
    g.stops = std::min(static_cast<int>(list.size()), kRmlMaxStops);
    for (int i = 0; i < g.stops; ++i) {
      g.positions[static_cast<std::size_t>(i)] = list[static_cast<std::size_t>(i)].position.number;
      g.colors[static_cast<std::size_t>(i)] = colorf(list[static_cast<std::size_t>(i)].color);
    }
  }
  const std::uint64_t h = next();
  gradients_.emplace(h, g);
  return static_cast<Rml::CompiledShaderHandle>(h);
}

void RmlRender::RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                             Rml::TextureHandle /*texture*/) {
  RmlCmd c;
  c.op = RmlOp::Shader;
  c.shader = static_cast<std::uint64_t>(shader);
  c.geometry = static_cast<std::uint64_t>(geometry);
  c.translation = {translation.x, translation.y};
  cmds_.push_back(c);
}

void RmlRender::ReleaseShader(Rml::CompiledShaderHandle shader) { deadGradients_.push_back(static_cast<std::uint64_t>(shader)); }

}  // namespace rpg::client
