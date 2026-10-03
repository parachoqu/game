#include "client/ui/UiDecorators.h"

#include <algorithm>
#include <cmath>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Decorator.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/RenderBox.h>
#include <RmlUi/Core/RenderManager.h>

namespace rpg::client {

namespace {

// `--rivet` da demo: radial-gradient(#e2c489 0 1px, bronze 1–2 px, #3a2e1c 2–3 px, preto a 50 % 3–3,6 px),
// numa imagem de 10 × 10 a 6 px das bordas da caixa de preenchimento: o centro fica a 11 px dos cantos.
class Rivets final : public Rml::Decorator {
 public:
  Rml::DecoratorDataHandle GenerateElementData(Rml::Element* element, Rml::BoxArea /*paint_area*/) const override {
    const Rml::RenderBox box = element->GetRenderBox(Rml::BoxArea::Padding);
    const float opacity = element->GetComputedValues().opacity();
    const Rml::Vector2f o = box.GetFillOffset(), s = box.GetFillSize();
    if (s.x < 24 || s.y < 24) return Rml::Decorator::INVALID_DECORATORDATAHANDLE;
    struct Ring {
      float r;
      Rml::Colourb c;
    };
    static const Ring rings[] = {{3.6f, {0, 0, 0, 128}}, {3.0f, {0x3a, 0x2e, 0x1c, 255}}, {2.0f, {0xa5, 0x84, 0x4f, 255}}, {1.0f, {0xe2, 0xc4, 0x89, 255}}};
    Rml::Mesh mesh;
    const Rml::Vector2f centers[4] = {{o.x + 11, o.y + 11}, {o.x + s.x - 11, o.y + 11}, {o.x + 11, o.y + s.y - 11}, {o.x + s.x - 11, o.y + s.y - 11}};
    constexpr int kSeg = 14;
    for (const Rml::Vector2f& c : centers)
      for (const Ring& ring : rings) {
        const auto first = static_cast<int>(mesh.vertices.size());
        const Rml::ColourbPremultiplied col = ring.c.ToPremultiplied(opacity);
        mesh.vertices.push_back({c, col, {0, 0}});
        for (int i = 0; i < kSeg; ++i) {
          const float a = static_cast<float>(i) / kSeg * 6.2831853f;
          mesh.vertices.push_back({{c.x + std::cos(a) * ring.r, c.y + std::sin(a) * ring.r}, col, {0, 0}});
        }
        for (int i = 0; i < kSeg; ++i) {
          mesh.indices.push_back(first);
          mesh.indices.push_back(first + 1 + i);
          mesh.indices.push_back(first + 1 + (i + 1) % kSeg);
        }
      }
    auto* g = new Rml::Geometry(element->GetRenderManager()->MakeGeometry(std::move(mesh)));
    return reinterpret_cast<Rml::DecoratorDataHandle>(g);
  }
  void ReleaseElementData(Rml::DecoratorDataHandle data) const override { delete reinterpret_cast<Rml::Geometry*>(data); }
  void RenderElement(Rml::Element* element, Rml::DecoratorDataHandle data) const override {
    reinterpret_cast<Rml::Geometry*>(data)->Render(element->GetAbsoluteOffset(Rml::BoxArea::Border));
  }
};

class RivetsInstancer final : public Rml::DecoratorInstancer {
 public:
  RivetsInstancer() { RegisterShorthand("decorator", "", Rml::ShorthandType::FallThrough); }
  Rml::SharedPtr<Rml::Decorator> InstanceDecorator(const Rml::String&, const Rml::PropertyDictionary&,
                                                   const Rml::DecoratorInstancerInterface&) override {
    return Rml::MakeShared<Rivets>();
  }
};

}  // namespace

std::vector<std::unique_ptr<Rml::DecoratorInstancer>> registerUiDecorators() {
  std::vector<std::unique_ptr<Rml::DecoratorInstancer>> out;
  out.push_back(std::make_unique<RivetsInstancer>());
  Rml::Factory::RegisterDecoratorInstancer("rivets", out.back().get());
  return out;
}

std::vector<std::uint8_t> stoneGrain(int size) {
  // ruído de valor em duas oitavas (como o feTurbulence fractalNoise do SVG), cinza, alfa ~5 %
  const auto hash = [](int x, int y) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
  };
  const auto noise = [&](float x, float y, int period) {
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(x0), fy = y - static_cast<float>(y0);
    const auto at = [&](int i, int j) { return hash(((i % period) + period) % period, ((j % period) + period) % period); };
    const float a = at(x0, y0), b = at(x0 + 1, y0), c = at(x0, y0 + 1), d = at(x0 + 1, y0 + 1);
    const float u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
  };
  std::vector<std::uint8_t> px(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4);
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) {
      const float n = 0.65f * noise(static_cast<float>(x), static_cast<float>(y), size) +
                      0.35f * noise(static_cast<float>(x) * 2.0f, static_cast<float>(y) * 2.0f, size * 2);
      const float alpha = 0.05f;
      const auto i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4;
      const auto g = static_cast<std::uint8_t>(std::lround(std::clamp(n, 0.0f, 1.0f) * alpha * 255.0f));
      px[i] = px[i + 1] = px[i + 2] = g;
      px[i + 3] = static_cast<std::uint8_t>(std::lround(alpha * 255.0f));
    }
  return px;
}

}  // namespace rpg::client
