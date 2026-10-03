#include "client/assets/ScenePack.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

#include <meshoptimizer.h>
#include <nlohmann/json.hpp>

#include "core/Math.h"
#include "core/Paths.h"
#include "core/world/StaticWorld.h"

namespace rpg::client {

using nlohmann::json;

namespace {

[[noreturn]] void bad(const std::string& what) { throw std::runtime_error("cpp/assets/client: " + what); }

glm::vec3 vec3(const json& j) { return {j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()}; }

glm::mat4 mat4(const json& j) {
  if (!j.is_array() || j.size() != 16) bad("matriz inválida");
  glm::mat4 m;
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r) m[c][r] = j.at(static_cast<std::size_t>(c * 4 + r)).get<float>();
  return m;
}

PackStream stream(const json& j) {
  PackStream s;
  s.offset = j.at("offset").get<std::uint64_t>();
  s.bytes = j.at("bytes").get<std::uint64_t>();
  s.count = j.at("count").get<std::uint32_t>();
  s.stride = j.at("stride").get<std::uint32_t>();
  const std::string codec = j.at("codec").get<std::string>();
  s.codec = codec == "meshopt-v" ? PackStream::Codec::MeshoptVertex : codec == "meshopt-i" ? PackStream::Codec::MeshoptIndex : PackStream::Codec::Raw;
  return s;
}

Wrap wrap(const std::string& s) { return s == "repeat" ? Wrap::Repeat : s == "mirror" ? Wrap::Mirror : Wrap::Clamp; }

std::uint32_t featureBit(const std::string& f) {
  if (f == "tri") return kFeatTri;
  if (f == "occ") return kFeatOcc;
  if (f == "splat") return kFeatSplat;
  if (f == "wind") return kFeatWind;
  if (f == "grass") return kFeatGrass;
  if (f == "water") return kFeatWater;
  if (f == "shore") return kFeatShore;
  if (f == "anomaly") return kFeatAnomaly;
  bad("recurso de material desconhecido: " + f);
}

PackDraw draw(const json& j) {
  PackDraw d;
  d.geometry = j.at("geometry").get<int>();
  d.material = j.at("material").get<int>();
  d.start = j.value("start", 0u);
  d.count = j.value("count", 0u);
  d.matrix = mat4(j.at("matrix"));
  d.map = j.value("map", 0);
  d.renderOrder = j.value("renderOrder", 0);
  d.group = j.value("group", std::string{});
  d.name = j.value("name", std::string{});
  return d;
}

std::vector<PackDraw> draws(const json& j) {
  std::vector<PackDraw> out;
  for (const auto& d : j) out.push_back(draw(d));
  return out;
}

PackModel model(const json& j) {
  PackModel m;
  m.matrix = mat4(j.at("matrix"));
  m.x = j.value("x", 0.0);
  m.z = j.value("z", 0.0);
  m.kind = j.value("kind", std::string{});
  m.parts = draws(j.at("parts"));
  return m;
}

PackTerrainLod terrainLod(const json& j) {
  PackTerrainLod l;
  l.n = j.at("n").get<int>();
  l.step = j.at("step").get<double>();
  l.splat = stream(j.at("splat"));
  l.hash = j.at("hash").get<std::uint32_t>();
  return l;
}

}  // namespace

ScenePack ScenePack::load(const std::filesystem::path& dir) {
  ScenePack P;
  P.dir = dir;
  std::ifstream jf(dir / "scene.json");
  if (!jf) bad("não encontrei " + pathToUtf8(dir / "scene.json") + " (rode demo/tools/bake-client-scene.mjs)");
  const json J = json::parse(jf);
  if (J.at("schema").get<int>() != 1) bad("schema desconhecido");
  {
    const auto binInfo = J.at("bin");
    std::ifstream bf(dir / binInfo.at("file").get<std::string>(), std::ios::binary);
    if (!bf) bad("não encontrei scene.bin");
    P.bin.assign(std::istreambuf_iterator<char>(bf), std::istreambuf_iterator<char>());
    if (P.bin.size() != binInfo.at("bytes").get<std::size_t>()) bad("scene.bin com tamanho diferente do scene.json");
  }
  for (const auto& s : J.at("sources")) {
    PackSource src;
    src.w = s.at("w").get<int>();
    src.h = s.at("h").get<int>();
    src.files = s.at("files").get<std::vector<std::string>>();
    P.sources.push_back(std::move(src));
  }
  for (const auto& t : J.at("textures")) {
    PackTexture tex;
    tex.source = t.at("source").get<int>();
    tex.flipY = t.at("flipY").get<bool>();
    tex.srgb = t.at("srgb").get<bool>();
    tex.array = t.at("array").get<bool>();
    tex.mipmaps = t.at("mipmaps").get<bool>();
    tex.nearest = t.value("nearest", false);
    tex.anisotropy = t.value("anisotropy", 1);
    tex.wrapS = wrap(t.at("wrapS").get<std::string>());
    tex.wrapT = wrap(t.at("wrapT").get<std::string>());
    for (const auto& m : t.at("mipLevels")) tex.mipLevels.push_back({m.at("w").get<int>(), m.at("h").get<int>(), m.at("file").get<std::string>()});
    const auto& uv = t.at("uvMatrix");
    for (std::size_t i = 0; i < 9; ++i) tex.uvMatrix[i] = uv.at(i).get<float>();
    P.textures.push_back(std::move(tex));
  }
  for (const auto& m : J.at("materials")) {
    PackMaterial mat;
    mat.name = m.value("name", std::string{});
    mat.type = m.at("type").get<std::string>();
    for (const auto& f : m.at("feats")) mat.features |= featureBit(f.get<std::string>());
    for (const auto& [k, v] : m.at("uniforms").items()) {
      if (v.is_number()) mat.uniforms[k] = v.get<double>();
      else if (v.is_boolean()) mat.uniforms[k] = v.get<bool>() ? 1.0 : 0.0;
      else if (v.is_array()) mat.uniforms[k] = v.get<std::vector<double>>();
      else if (v.is_object() && v.contains("texture")) mat.uniforms[k] = v.at("texture").get<int>();
    }
    mat.color = vec3(m.at("color"));
    mat.emissive = vec3(m.at("emissive"));
    mat.emissiveIntensity = m.at("emissiveIntensity").get<float>();
    mat.roughness = m.at("roughness").get<float>();
    mat.metalness = m.at("metalness").get<float>();
    mat.opacity = m.at("opacity").get<float>();
    mat.transparent = m.at("transparent").get<bool>();
    mat.alphaTest = m.at("alphaTest").get<float>();
    mat.side = m.at("side").get<int>();
    mat.vertexColors = m.at("vertexColors").get<bool>();
    mat.flatShading = m.at("flatShading").get<bool>();
    mat.depthWrite = m.at("depthWrite").get<bool>();
    mat.depthTest = m.at("depthTest").get<bool>();
    mat.additive = m.at("additive").get<bool>();
    if (!m.at("polygonOffset").is_null()) mat.polygonOffset = glm::vec2(m.at("polygonOffset").at(0).get<float>(), m.at("polygonOffset").at(1).get<float>());
    mat.envMapIntensity = m.at("envMapIntensity").get<float>();
    mat.normalScale = {m.at("normalScale").at(0).get<float>(), m.at("normalScale").at(1).get<float>()};
    mat.map = m.at("map").get<int>();
    mat.normalMap = m.at("normalMap").get<int>();
    mat.roughnessMap = m.at("roughnessMap").get<int>();
    mat.metalnessMap = m.at("metalnessMap").get<int>();
    mat.emissiveMap = m.at("emissiveMap").get<int>();
    mat.aoMap = m.at("aoMap").get<int>();
    mat.alphaMap = m.at("alphaMap").get<int>();
    P.materials.push_back(std::move(mat));
  }
  for (const auto& g : J.at("geometries")) {
    PackGeometry geo;
    geo.count = g.at("count").get<std::uint32_t>();
    for (const auto& [k, a] : g.at("attributes").items()) {
      PackAttribute att;
      att.normalizedU8 = a.at("type").get<std::string>() == "u8n";
      att.u16 = a.at("type").get<std::string>() == "u16";
      att.size = a.at("size").get<int>();
      att.stream = stream(a);
      geo.attributes[k] = att;
    }
    if (!g.at("index").is_null()) geo.index = stream(g.at("index"));
    const auto& b = g.at("bounds");
    geo.boundsMin = {b.at(0).get<float>(), b.at(1).get<float>(), b.at(2).get<float>()};
    geo.boundsMax = {b.at(3).get<float>(), b.at(4).get<float>(), b.at(5).get<float>()};
    P.geometries.push_back(std::move(geo));
  }
  for (const auto& t : J.at("terrain")) {
    PackTerrainTile tile;
    tile.map = t.at("map").get<int>();
    tile.x0 = t.at("x0").get<double>();
    tile.z0 = t.at("z0").get<double>();
    tile.size = t.at("size").get<double>();
    tile.x = t.at("x").get<double>();
    tile.z = t.at("z").get<double>();
    tile.radius = t.at("radius").get<double>();
    tile.material = t.at("material").get<int>();
    tile.near = terrainLod(t.at("near"));
    tile.far = terrainLod(t.at("far"));
    P.terrain.push_back(tile);
  }
  P.statics = draws(J.at("static"));
  for (const auto& i : J.at("instanced")) {
    PackInstanced in;
    in.map = i.at("map").get<int>();
    in.geometry = i.at("geometry").get<int>();
    in.material = i.at("material").get<int>();
    in.count = i.at("count").get<std::uint32_t>();
    in.group = i.value("group", std::string{});
    in.matrices = stream(i.at("matrices"));
    if (!i.at("colors").is_null()) in.colors = stream(i.at("colors"));
    P.instanced.push_back(std::move(in));
  }
  for (const auto& f : J.at("lodFields")) {
    PackLodField field;
    field.name = f.at("name").get<std::string>();
    field.band = f.at("band").get<std::string>();
    field.secondary = f.at("secondary").get<bool>();
    field.map = f.at("map").get<int>();
    for (const auto& lv : f.at("levels")) {
      std::vector<PackLodPart> parts;
      for (const auto& p : lv) {
        PackLodPart part;
        part.geometry = p.at("geometry").get<int>();
        part.material = p.at("material").get<int>();
        const auto tint = p.at("tint");
        part.tint = tint.is_null() ? PackLodPart::Tint::None : tint.get<std::string>() == "fol" ? PackLodPart::Tint::Foliage : PackLodPart::Tint::Trunk;
        parts.push_back(part);
      }
      field.levels.push_back(std::move(parts));
    }
    for (const auto& c : f.at("chunks"))
      field.chunks.push_back({c.at("n").get<std::uint32_t>(), c.at("cx").get<float>(), c.at("cz").get<float>(), c.at("radius").get<float>(), stream(c.at("items"))});
    P.lodFields.push_back(std::move(field));
  }
  const auto& bands = J.at("lodBands");
  P.lodBands["tree"] = bands.at("tree").get<std::vector<float>>();
  P.lodBands["shrub"] = bands.at("shrub").get<std::vector<float>>();
  P.shadowFar = bands.at("shadowFar").get<double>();
  const auto& dyn = J.at("dynamic");
  for (const auto& n : dyn.at("nodes")) P.nodes.push_back(model(n));
  for (const auto& s : dyn.at("shrines")) P.shrines.push_back(model(s));
  if (dyn.contains("campPalisade")) P.campPalisade = model(dyn.at("campPalisade"));
  if (J.contains("environment")) {
    const auto& e = J.at("environment");
    const auto hex = [](const json& c) { return static_cast<std::uint32_t>(std::stoul(c.get<std::string>().substr(1), nullptr, 16)); };
    P.environment.present = true;
    P.environment.file = e.at("file").get<std::string>();
    P.environment.w = e.at("width").get<int>();
    P.environment.h = e.at("height").get<int>();
    P.environment.zenith = hex(e.at("colors").at("zenith"));
    P.environment.horizon = hex(e.at("colors").at("horizon"));
    P.environment.ground = hex(e.at("colors").at("ground"));
  }
  for (const auto& [k, v] : J.at("templates").items()) {
    if (k == "weapons") {
      for (const auto& [fam, parts] : v.items()) P.weapons[fam] = draws(parts);
    } else if (k == "projectiles") {
      for (const auto& [kind, parts] : v.items()) P.projectiles[kind] = draws(parts);
    } else {
      P.templates[k] = draws(v);
    }
  }
  if (J.contains("rigs")) {
    const auto vec3 = [](const json& j) { return glm::vec3(j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()); };
    const auto vec4 = [](const json& j) { return glm::vec4(j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>(), j.at(3).get<float>()); };
    for (const auto& [id, r] : J.at("rigs").items()) {
      PackRig rig;
      rig.kind = r.at("kind").get<std::string>();
      for (const auto& n : r.at("nodes")) {
        rig.names.push_back(n.at("name").get<std::string>());
        rig.parent.push_back(n.at("parent").get<int>());
        rig.bone.push_back(n.at("bone").get<bool>());
      }
      rig.rest = stream(r.at("rest"));
      for (const auto& m : r.at("meshes")) {
        PackRigMesh mesh;
        mesh.node = m.at("node").get<int>();
        mesh.geometry = m.at("geometry").get<int>();
        mesh.material = m.at("material").get<int>();
        mesh.skinned = m.at("skinned").get<bool>();
        if (mesh.skinned) {
          mesh.bindMatrix = stream(m.at("bindMatrix"));
          mesh.boneInverses = stream(m.at("boneInverses"));
          mesh.bones = m.at("bones").get<std::vector<int>>();
        }
        rig.meshes.push_back(std::move(mesh));
      }
      for (const auto& [key, c] : r.at("clips").items()) {
        PackClip clip;
        clip.duration = c.at("duration").get<double>();
        for (const auto& t : c.at("tracks")) {
          PackTrack tr;
          tr.node = t.at("node").get<int>();
          const std::string path = t.at("path").get<std::string>();
          tr.path = path == "position" ? PackTrack::Path::Position : path == "quaternion" ? PackTrack::Path::Quaternion
                  : path == "scale" ? PackTrack::Path::Scale : PackTrack::Path::Other;
          tr.count = t.at("count").get<std::uint32_t>();
          tr.size = t.at("size").get<std::uint32_t>();
          tr.step = t.at("interp").get<std::string>() == "step";
          tr.times = stream(t.at("times"));
          tr.values = stream(t.at("values"));
          clip.tracks.push_back(tr);
        }
        rig.clips[key] = std::move(clip);
      }
      if (r.contains("clipOrder")) rig.clipOrder = r.at("clipOrder").get<std::vector<std::string>>();
      else for (const auto& [key, c] : rig.clips) rig.clipOrder.push_back(key);
      rig.hipsY = r.value("hipsY", 0.0);
      rig.hipsBase = r.value("hipsBase", 0.0);
      rig.height = r.value("height", 0.0);
      rig.seatY = r.value("seatY", 0.0);
      rig.seatZ = r.value("seatZ", 0.0);
      if (r.contains("idleRef"))
        for (const auto& [k, q] : r.at("idleRef").items()) rig.idleRef[k] = vec4(q);
      if (r.contains("cycle"))
        for (const auto& [k, c] : r.at("cycle").items()) rig.cycle[k] = {c.at("dur").get<double>(), c.at("off").get<double>(), c.at("speed").get<double>()};
      if (r.contains("grips")) {
        const auto grip = [&](const json& g) { return PackGrip{g.at("bone").get<std::string>(), vec3(g.at("t")), vec3(g.at("s")), vec4(g.at("r"))}; };
        rig.gripR = grip(r.at("grips").at("R"));
        rig.gripL = grip(r.at("grips").at("L"));
      }
      P.rigs[id] = std::move(rig);
    }
  }
  if (J.contains("clipMeta") && J.at("clipMeta").is_object()) {
    for (const auto& [key, m] : J.at("clipMeta").items()) {
      PackClipMeta meta;
      meta.contactMode = m.value("contactMode", std::string{"none"});
      meta.locomotionMode = m.value("locomotionMode", std::string{"oneshot"});
      meta.group = m.value("group", std::string{});
      if (m.contains("motion") && m.at("motion").is_array()) meta.motion = std::array<double, 2>{m.at("motion").at(0).get<double>(), m.at("motion").at(1).get<double>()};
      P.clipMeta[key] = meta;
    }
  }
  return P;
}

std::vector<std::uint8_t> ScenePack::decodeVertices(const PackStream& s) const {
  if (s.offset + s.bytes > bin.size()) bad("fluxo fora do scene.bin");
  const std::uint8_t* src = bin.data() + s.offset;
  std::vector<std::uint8_t> out(static_cast<std::size_t>(s.count) * s.stride);
  if (s.codec == PackStream::Codec::Raw) {
    if (s.bytes != out.size()) bad("fluxo cru com tamanho errado");
    std::memcpy(out.data(), src, out.size());
  } else if (meshopt_decodeVertexBuffer(out.data(), s.count, s.stride, src, s.bytes) != 0) {
    bad("falha ao decodificar fluxo de vértices");
  }
  return out;
}

std::vector<std::uint32_t> ScenePack::decodeIndices(const PackStream& s) const {
  if (s.offset + s.bytes > bin.size()) bad("fluxo fora do scene.bin");
  const std::uint8_t* src = bin.data() + s.offset;
  std::vector<std::uint32_t> out(s.count);
  if (s.codec == PackStream::Codec::Raw) {
    if (s.bytes != s.count * 4ull) bad("índices crus com tamanho errado");
    std::memcpy(out.data(), src, s.bytes);
  } else if (meshopt_decodeIndexBuffer(out.data(), s.count, 4, src, s.bytes) != 0) {
    bad("falha ao decodificar índices");
  }
  return out;
}

std::vector<float> ScenePack::floats(const PackStream& s) const {
  const std::vector<std::uint8_t> raw = decodeVertices(s);
  std::vector<float> out(raw.size() / sizeof(float));
  std::memcpy(out.data(), raw.data(), out.size() * sizeof(float));
  return out;
}

std::vector<float> ScenePack::attributeFloats(const PackGeometry& g, std::string_view name) const {
  const auto it = g.attributes.find(name);
  if (it == g.attributes.end()) return {};
  const PackAttribute& a = it->second;
  const std::vector<std::uint8_t> raw = decodeVertices(a.stream);
  std::vector<float> out(static_cast<std::size_t>(g.count) * static_cast<std::size_t>(a.size));
  if (a.normalizedU8) {
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = static_cast<float>(raw[i]) / 255.0f;
  } else if (a.u16) {
    for (std::size_t i = 0; i < out.size(); ++i) {
      std::uint16_t v = 0;
      std::memcpy(&v, &raw[i * 2], 2);
      out[i] = static_cast<float>(v);
    }
  } else {
    std::memcpy(out.data(), raw.data(), out.size() * sizeof(float));
  }
  return out;
}

TerrainTileMesh buildTerrainTile(const StaticMap& map, double x0, double z0, int n, double step) {
  TerrainTileMesh m;
  const MapTerrain& t = map.terrain();
  const auto h = [&](double x, double z) { return t.terrainHeight(x, z); };
  m.positions.resize(static_cast<std::size_t>(n) * n * 3);
  m.normals.resize(static_cast<std::size_t>(n) * n * 3);
  for (int j = 0; j < n; ++j) {
    for (int i = 0; i < n; ++i) {
      const auto k = static_cast<std::size_t>(j * n + i);
      const double x = x0 + i * step, z = z0 + j * step;
      m.positions[k * 3] = static_cast<float>(x);
      m.positions[k * 3 + 1] = static_cast<float>(h(x, z));
      m.positions[k * 3 + 2] = static_cast<float>(z);
      // scene-world.js normalAt: diferenças centrais a 1 m, independentes do passo do LOD
      const double l = h(x - 1, z), r = h(x + 1, z), d = h(x, z - 1), u = h(x, z + 1);
      const double nx = l - r, nz = d - u, ny = 2;
      const double inv = 1 / jsHypot3(nx, ny, nz);
      m.normals[k * 3] = static_cast<float>(nx * inv);
      m.normals[k * 3 + 1] = static_cast<float>(ny * inv);
      m.normals[k * 3 + 2] = static_cast<float>(nz * inv);
    }
  }
  for (int j = 0; j < n - 1; ++j)
    for (int i = 0; i < n - 1; ++i) {
      const auto a = static_cast<std::uint32_t>(j * n + i), b = a + 1, c = a + static_cast<std::uint32_t>(n), d = c + 1;
      m.indices.insert(m.indices.end(), {a, c, d, a, d, b});  // diagonal a–d, igual à amostragem
    }
  return m;
}

std::uint32_t terrainTileHash(const TerrainTileMesh& m) {
  std::uint32_t h = 0x811c9dc5u;
  for (const std::vector<float>* arr : {&m.positions, &m.normals}) {
    const auto* b = reinterpret_cast<const std::uint8_t*>(arr->data());
    for (std::size_t i = 0; i < arr->size() * sizeof(float); ++i) {
      h ^= b[i];
      h *= 0x01000193u;
    }
  }
  return h;
}

}  // namespace rpg::client
