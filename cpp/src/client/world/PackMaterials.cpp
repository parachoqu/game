#include "client/world/PackMaterials.h"

namespace rpg::client {

namespace {

double num(const PackMaterial& m, std::string_view k, double def) {
  const auto it = m.uniforms.find(k);
  if (it == m.uniforms.end()) return def;
  if (const auto* d = std::get_if<double>(&it->second)) return *d;
  return def;
}

int tex(const PackMaterial& m, std::string_view k) {
  const auto it = m.uniforms.find(k);
  if (it == m.uniforms.end()) return -1;
  if (const auto* t = std::get_if<int>(&it->second)) return *t;
  return -1;
}

const std::vector<double>* vec(const PackMaterial& m, std::string_view k) {
  const auto it = m.uniforms.find(k);
  if (it == m.uniforms.end()) return nullptr;
  return std::get_if<std::vector<double>>(&it->second);
}

}  // namespace

MaterialSetup setupMaterial(const ScenePack& pack, const PackMaterial& m) {
  MaterialSetup s;
  MaterialBlock& b = s.block;
  b.color[0] = m.color.r;
  b.color[1] = m.color.g;
  b.color[2] = m.color.b;
  b.color[3] = m.opacity;
  for (int i = 0; i < 3; ++i) b.emissive[i] = m.emissive[i] * m.emissiveIntensity;
  b.pbr[0] = m.roughness;
  b.pbr[1] = m.metalness;
  b.pbr[2] = m.alphaTest;
  b.tri[0] = static_cast<float>(num(m, "uScale", 1));
  b.tri[1] = static_cast<float>(num(m, "uNStr", 1));
  b.tri[2] = m.normalScale.x;
  b.tri[3] = m.normalScale.y;
  b.tint[0] = b.tint[1] = b.tint[2] = b.tint[3] = 1.0f;
  if (const auto* t = vec(m, "uTint"); t && t->size() >= 3)
    for (std::size_t i = 0; i < 3; ++i) b.tint[i] = static_cast<float>((*t)[i]);
  if (const auto* t = vec(m, "uTScales"))
    for (std::size_t i = 0; i < t->size() && i < 12; ++i) b.tscale[i] = static_cast<float>((*t)[i]);

  b.flags[0] = static_cast<std::int32_t>(m.features);
  b.flags[1] = m.unlit() ? 2 : m.lambert() ? 1 : 0;
  int maps = 0;
  s.terrain = (m.features & kFeatSplat) != 0;
  if (s.terrain) {
    s.textures = {tex(m, "uTA"), tex(m, "uTN"), tex(m, "uMacro"), -1, -1};
  } else if (m.features & kFeatTri) {
    s.textures[0] = tex(m, "uAlb");
    s.textures[1] = tex(m, "uNrm");
  } else {
    s.textures[0] = m.map;
    s.textures[1] = (m.features & kFeatWater) ? tex(m, "uWaterN") : m.normalMap;
    s.textures[2] = m.roughnessMap;
    s.textures[3] = m.metalnessMap;
    s.textures[4] = m.aoMap;
    if (m.map >= 0) maps |= 1;
    if (m.normalMap >= 0 && !(m.features & kFeatWater)) maps |= 2;
    if (m.roughnessMap >= 0) maps |= 4;
    if (m.metalnessMap >= 0) maps |= 8;
    if (m.aoMap >= 0) maps |= 16;
  }
  b.flags[2] = maps;
  b.flags[3] = (m.flatShading ? 1 : 0) | (m.side == 2 ? 2 : 0) | (m.transparent ? 4 : 0);

  DrawBlock& d = s.draw;
  d.wind[0] = static_cast<float>(num(m, "uWind", 1));
  d.wind[1] = static_cast<float>(((m.features & kFeatWind) ? 1 : 0) | ((m.features & kFeatGrass) ? 2 : 0));
  // matriz da textura do mapa principal (Matrix3 do three, coluna a coluna)
  const int uvTex = m.map >= 0 ? m.map : m.normalMap;
  std::array<float, 9> u{1, 0, 0, 0, 1, 0, 0, 0, 1};
  if (uvTex >= 0 && static_cast<std::size_t>(uvTex) < pack.textures.size()) u = pack.textures[static_cast<std::size_t>(uvTex)].uvMatrix;
  d.uvRow0[0] = u[0];
  d.uvRow0[1] = u[3];
  d.uvRow0[2] = u[6];
  d.uvRow1[0] = u[1];
  d.uvRow1[1] = u[4];
  d.uvRow1[2] = u[7];
  d.flags[0] = m.vertexColors ? 1.0f : 0.0f;

  s.transparent = m.transparent;
  s.cull = m.side == 2 ? Cull::None : m.side == 1 ? Cull::Front : Cull::Back;
  s.blend = m.additive ? Blend::Additive : m.transparent ? Blend::Alpha : Blend::Opaque;
  s.depthWrite = m.depthWrite;
  s.depthTest = m.depthTest;
  if (m.polygonOffset) {
    s.depthBias = true;
    s.biasFactor = m.polygonOffset->x;
    s.biasUnits = m.polygonOffset->y;
  }
  return s;
}

}  // namespace rpg::client
