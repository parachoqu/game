#include "client/world/PackScene.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <glm/gtc/packing.hpp>

#include "core/world/StaticWorld.h"

namespace rpg::client {

namespace {

constexpr float kLodHyst = 3.0f;                         // lod-field.js HYST
constexpr glm::vec2 kTurbCenter{1400.0f, 0.0f};          // TURB_RUNTIME_OFFSET

std::uint8_t levelOf(const std::vector<float>& bands, float d) {
  for (std::size_t i = 0; i < bands.size(); ++i)
    if (d < bands[i]) return static_cast<std::uint8_t>(i);
  return 255;
}

void boundsOf(const MeshSlice& s, const glm::mat4& m, glm::vec3& center, float& radius) {
  glm::vec3 lo(1e30f), hi(-1e30f);
  for (int i = 0; i < 8; ++i) {
    const glm::vec3 p((i & 1) ? s.boundsMax.x : s.boundsMin.x, (i & 2) ? s.boundsMax.y : s.boundsMin.y, (i & 4) ? s.boundsMax.z : s.boundsMin.z);
    const glm::vec3 w = glm::vec3(m * glm::vec4(p, 1.0f));
    lo = glm::min(lo, w);
    hi = glm::max(hi, w);
  }
  center = (lo + hi) * 0.5f;
  radius = glm::length(hi - lo) * 0.5f;
}

// Planos do frustum (Gribb–Hartmann) para profundidade 0–1.
struct Frustum {
  glm::vec4 p[6];
  explicit Frustum(const glm::mat4& m) {
    const auto row = [&](int i) { return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]); };
    p[0] = row(3) + row(0);
    p[1] = row(3) - row(0);
    p[2] = row(3) + row(1);
    p[3] = row(3) - row(1);
    p[4] = row(2);
    p[5] = row(3) - row(2);
    for (auto& v : p) v /= glm::length(glm::vec3(v));
  }
  bool sphere(glm::vec3 c, float r) const {
    for (const auto& v : p)
      if (glm::dot(glm::vec3(v), c) + v.w < -r) return false;
    return true;
  }
};

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

}  // namespace

// ---------------------------------------------------------------- malhas
PackMeshes buildPackMeshes(const ScenePack& pack, const StaticWorld& world) {
  PackMeshes M;
  std::size_t vTotal = 0;
  for (const PackGeometry& g : pack.geometries) vTotal += g.count;
  for (const PackTerrainTile& t : pack.terrain) vTotal += static_cast<std::size_t>(t.near.n * t.near.n + t.far.n * t.far.n);
  M.vertices.reserve(vTotal);

  const auto half = [](float f) { return glm::packHalf1x16(f); };
  for (const PackGeometry& g : pack.geometries) {
    MeshSlice s;
    s.baseVertex = static_cast<std::int32_t>(M.vertices.size());
    s.firstIndex = static_cast<std::uint32_t>(M.indices.size());
    s.boundsMin = g.boundsMin;
    s.boundsMax = g.boundsMax;
    const std::vector<float> pos = pack.attributeFloats(g, "position");
    const std::vector<float> nrm = pack.attributeFloats(g, "normal");
    const std::vector<float> uv = pack.attributeFloats(g, "uv");
    const std::vector<float> col = pack.attributeFloats(g, "color");
    const std::vector<float> dep = pack.attributeFloats(g, "aDepth");
    const auto colIt = g.attributes.find("color");
    const std::size_t cs = colIt == g.attributes.end() ? 0 : static_cast<std::size_t>(colIt->second.size);
    for (std::size_t i = 0; i < g.count; ++i) {
      PackVertex v{};
      for (std::size_t k = 0; k < 3; ++k) v.pos[k] = pos[i * 3 + k];
      if (!nrm.empty())
        for (std::size_t k = 0; k < 3; ++k) v.normal[k] = nrm[i * 3 + k];
      else
        v.normal[1] = 1.0f;
      if (!uv.empty()) {
        v.uv[0] = uv[i * 2];
        v.uv[1] = uv[i * 2 + 1];
      }
      for (std::size_t k = 0; k < 4; ++k) v.color[k] = half(cs && k < cs ? col[i * cs + k] : 1.0f);
      v.depth = dep.empty() ? 0.0f : dep[i];
      M.vertices.push_back(v);
    }
    // pele (personagens): índices dos ossos e pesos, num fluxo à parte
    if (g.attributes.contains("skinIndex") && g.attributes.contains("skinWeight")) {
      const std::vector<float> ji = pack.attributeFloats(g, "skinIndex");
      const std::vector<float> jw = pack.attributeFloats(g, "skinWeight");
      if (ji.size() >= g.count * 4 && jw.size() >= g.count * 4) {
        s.skinBase = static_cast<std::int32_t>(M.skin.size());
        for (std::size_t i = 0; i < g.count; ++i) {
          SkinVertex sv{};
          for (std::size_t k = 0; k < 4; ++k) {
            sv.joints[k] = static_cast<std::uint16_t>(ji[i * 4 + k]);
            sv.weights[k] = jw[i * 4 + k];
          }
          M.skin.push_back(sv);
        }
      }
    }
    if (g.index) {
      const std::vector<std::uint32_t> idx = pack.decodeIndices(*g.index);
      M.indices.insert(M.indices.end(), idx.begin(), idx.end());
      s.indexCount = static_cast<std::uint32_t>(idx.size());
    } else {
      for (std::uint32_t i = 0; i < g.count; ++i) M.indices.push_back(i);
      s.indexCount = g.count;
    }
    M.geometries.push_back(s);
  }

  for (const PackTerrainTile& t : pack.terrain) {
    TerrainSlice ts;
    ts.map = t.map;
    ts.material = t.material;
    ts.x = static_cast<float>(t.x);
    ts.z = static_cast<float>(t.z);
    ts.flatRadius = static_cast<float>(t.radius);
    const StaticMap& map = world.map(t.map == 1 ? MapKind::Turbulent : MapKind::Region);
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (int l = 0; l < 2; ++l) {
      const PackTerrainLod& L = l == 0 ? t.near : t.far;
      const TerrainTileMesh tile = buildTerrainTile(map, t.x0, t.z0, L.n, L.step);
      const std::vector<std::uint8_t> splat = pack.decodeVertices(L.splat);
      MeshSlice& s = ts.lod[static_cast<std::size_t>(l)];
      s.baseVertex = static_cast<std::int32_t>(M.vertices.size());
      s.firstIndex = static_cast<std::uint32_t>(M.indices.size());
      const std::size_t n = tile.positions.size() / 3;
      for (std::size_t i = 0; i < n; ++i) {
        PackVertex v{};
        for (std::size_t k = 0; k < 3; ++k) {
          v.pos[k] = tile.positions[i * 3 + k];
          v.normal[k] = tile.normals[i * 3 + k];
        }
        for (std::size_t k = 0; k < 4; ++k) v.color[k] = half(1.0f);
        if (splat.size() >= (i + 1) * 12) std::memcpy(v.splat, &splat[i * 12], 12);
        lo = glm::min(lo, glm::vec3(v.pos[0], v.pos[1], v.pos[2]));
        hi = glm::max(hi, glm::vec3(v.pos[0], v.pos[1], v.pos[2]));
        M.vertices.push_back(v);
      }
      M.indices.insert(M.indices.end(), tile.indices.begin(), tile.indices.end());
      s.indexCount = static_cast<std::uint32_t>(tile.indices.size());
      s.boundsMin = lo;
      s.boundsMax = hi;
    }
    ts.center = (lo + hi) * 0.5f;
    ts.radius = glm::length(hi - lo) * 0.5f;
    M.terrain.push_back(ts);
  }
  return M;
}

// ---------------------------------------------------------------- visão do mundo
WorldView::WorldView(const ScenePack& pack, const PackMeshes& meshes) : pack_(pack), meshes_(meshes) {
  // cenário estático: uma instância por desenho
  for (std::size_t i = 0; i < pack.statics.size(); ++i) {
    const PackDraw& d = pack.statics[i];
    StaticEntry e;
    e.draw = static_cast<int>(i);
    boundsOf(meshes.geometries[static_cast<std::size_t>(d.geometry)], d.matrix, e.center, e.radius);
    if (startsWith(d.group, "extra")) e.cullFar = e.radius;  // extra-props.js: o limite depende do tamanho
    e.mirrored = glm::determinant(glm::mat3(d.matrix)) < 0.0f;
    statics_.push_back(e);
    staticInstances_.push_back(makeInstance(d.matrix));
  }
  // InstancedMesh do bake (rochas da serra e da Turbulenta)
  for (std::size_t i = 0; i < pack.instanced.size(); ++i) {
    const PackInstanced& in = pack.instanced[i];
    InstancedEntry e;
    e.index = static_cast<int>(i);
    e.first = static_cast<std::uint32_t>(staticInstances_.size());
    const std::vector<std::uint8_t> mats = pack.decodeVertices(in.matrices);
    std::vector<std::uint8_t> cols;
    if (in.colors) cols = pack.decodeVertices(*in.colors);
    glm::vec3 lo(1e30f), hi(-1e30f);
    const MeshSlice& g = meshes.geometries[static_cast<std::size_t>(in.geometry)];
    for (std::uint32_t k = 0; k < in.count; ++k) {
      Instance inst{};
      std::memcpy(inst.m, &mats[static_cast<std::size_t>(k) * 64], 64);
      if (!cols.empty()) std::memcpy(inst.tint, &cols[static_cast<std::size_t>(k) * 12], 12);
      else inst.tint[0] = inst.tint[1] = inst.tint[2] = 1.0f;
      inst.tint[3] = 1.0f;
      glm::mat4 m;
      std::memcpy(&m[0][0], inst.m, 64);
      glm::vec3 c;
      float r = 0;
      boundsOf(g, m, c, r);
      lo = glm::min(lo, c - glm::vec3(r));
      hi = glm::max(hi, c + glm::vec3(r));
      staticInstances_.push_back(inst);
    }
    e.center = (lo + hi) * 0.5f;
    e.radius = glm::length(hi - lo) * 0.5f;
    instanced_.push_back(e);
  }
  // campos de LOD
  for (const PackLodField& f : pack.lodFields) {
    LodFieldState fs;
    fs.src = &f;
    fs.tree = f.band == "tree";
    float partTop = 1.0f;
    for (const auto& lv : f.levels)
      for (const PackLodPart& p : lv) partTop = std::max(partTop, meshes.geometries[static_cast<std::size_t>(p.geometry)].boundsMax.y);
    for (const PackLodChunk& c : f.chunks) {
      LodChunk lc;
      lc.n = c.n;
      lc.cx = c.cx;
      lc.cz = c.cz;
      lc.radius = c.radius;
      const std::vector<std::uint8_t> raw = pack.decodeVertices(c.items);
      lc.items.resize(c.n);
      lc.px.resize(c.n);
      lc.pz.resize(c.n);
      float y0 = 1e30f, y1 = -1e30f;
      for (std::uint32_t k = 0; k < c.n; ++k) {
        float v[20];
        std::memcpy(v, &raw[static_cast<std::size_t>(k) * 80], 80);
        Instance& it = lc.items[k];
        std::memcpy(it.m, v, 64);
        it.tint[0] = v[16];
        it.tint[1] = v[17];
        it.tint[2] = v[18];
        it.tint[3] = v[19];
        lc.px[k] = v[12];
        lc.pz[k] = v[14];
        const float sy = glm::length(glm::vec3(v[4], v[5], v[6]));
        y0 = std::min(y0, v[13]);
        y1 = std::max(y1, v[13] + partTop * sy);
      }
      lc.center = {c.cx, (y0 + y1) * 0.5f, c.cz};
      lc.sphereRadius = std::sqrt(c.radius * c.radius + (y1 - y0) * (y1 - y0) * 0.25f);
      lc.level.assign(c.n, 255);
      fs.chunks.push_back(std::move(lc));
    }
    fields_.push_back(std::move(fs));
  }
  tileNear_.assign(meshes.terrain.size(), 0);
  detail_.treeBands = pack.lodBands.count("tree") ? pack.lodBands.at("tree") : detail_.treeBands;
  detail_.shrubBands = pack.lodBands.count("shrub") ? pack.lodBands.at("shrub") : detail_.shrubBands;
}

void WorldView::setDetail(const WorldDetail& d) {
  detail_ = d;
  lodDirty_ = true;
}

// lod-field.js `updateChunk` (sem os sombreadores: fase 7)
void WorldView::updateChunk(LodChunk& c, const std::vector<float>& bands, std::uint32_t limit, glm::vec2 cam) {
  const float cut = bands.back();
  const float dc = std::hypot(c.cx - cam.x, c.cz - cam.y);
  const float dmin = std::max(0.0f, dc - c.radius), dmax = dc + c.radius;
  if (dmin > cut + kLodHyst) {
    if (c.mode != -1) {
      std::fill(c.level.begin(), c.level.end(), 255);
      c.mode = -1;
    }
    return;
  }
  const std::uint8_t lmin = levelOf(bands, dmin), lmax = levelOf(bands, dmax);
  if (lmin == lmax && lmin != 255) {
    if (c.mode != lmin || c.limit != limit) {
      std::fill(c.level.begin(), c.level.end(), 255);
      std::fill(c.level.begin(), c.level.begin() + limit, lmin);
      c.mode = lmin;
      c.limit = limit;
    }
    return;
  }
  for (std::uint32_t i = 0; i < c.n; ++i) {
    std::uint8_t l = 255;
    if (i < limit) {
      const float d = std::hypot(c.px[i] - cam.x, c.pz[i] - cam.y);
      l = levelOf(bands, d);
      const std::uint8_t p = c.level[i];
      if (p != l && p != 255) {
        const float edge = l == 255 ? cut : bands[std::min(p, l)];
        if (std::abs(d - edge) < kLodHyst && p < bands.size() && bands[p] > 0) l = p;
      }
    }
    c.level[i] = l;
  }
  c.mode = -3;
  c.limit = limit;
}

void WorldView::updateLod(glm::vec2 cam, bool force) {
  if (!force && !lodDirty_ && glm::length(cam - lastLodCam_) < 1.5f) return;
  lastLodCam_ = cam;
  const bool wasDirty = lodDirty_;
  lodDirty_ = false;
  for (LodFieldState& f : fields_) {
    const std::vector<float>& bands = f.tree ? detail_.treeBands : detail_.shrubBands;
    for (LodChunk& c : f.chunks) {
      if (wasDirty) c.mode = -2;
      const std::uint32_t limit = f.src->secondary ? static_cast<std::uint32_t>(std::floor(static_cast<double>(c.n) * static_cast<double>(detail_.secondaryDensity))) : c.n;
      updateChunk(c, bands, limit, cam);
    }
  }
}

std::uint32_t WorldView::activeCount(std::size_t field) const {
  std::uint32_t n = 0;
  for (const LodChunk& c : fields_[field].chunks)
    for (std::uint8_t l : c.level)
      if (l != 255 && l < fields_[field].src->levels.size()) ++n;
  return n;
}

void WorldView::update(glm::vec3 cam, const glm::mat4& viewProj, const glm::vec3& fwd, const DynamicSceneState& dyn, PackFrame& out) {
  out.terrain.clear();
  out.draws.clear();
  out.instances.clear();
  out.bones.clear();
  out.lodInstances = 0;
  const Frustum fr(viewProj);
  const glm::vec2 c2(cam.x, cam.z);
  const bool turbNear = glm::length(kTurbCenter - c2) < detail_.blockFar * 0.5f + 200.0f;
  const auto depthOf = [&](glm::vec3 p) { return glm::dot(p - cam, fwd); };

  // relevo: perto/longe com histerese
  for (std::size_t i = 0; i < meshes_.terrain.size(); ++i) {
    const TerrainSlice& t = meshes_.terrain[i];
    if (t.map == 1 && !turbNear) continue;
    const float d = std::hypot(t.x - cam.x, t.z - cam.z) - t.flatRadius;
    const bool isNear = d < detail_.tileNear + (tileNear_[i] ? 20.0f : -20.0f);
    tileNear_[i] = isNear ? 1 : 0;
    if (!fr.sphere(t.center, t.radius)) continue;
    out.terrain.emplace_back(static_cast<int>(i), isNear ? 0 : 1);
  }

  // cenário estático
  for (const StaticEntry& e : statics_) {
    const PackDraw& d = pack_.statics[static_cast<std::size_t>(e.draw)];
    if (d.map == 1 && !turbNear) continue;
    const float dist = std::hypot(e.center.x - cam.x, e.center.z - cam.z);
    if (e.cullFar > 0) {
      const float lim = (e.cullFar < 4 ? detail_.extraFar * 0.45f : detail_.extraFar) + e.cullFar;
      if (dist > lim) continue;
    } else if (dist - e.radius > detail_.blockFar) {
      continue;
    }
    if (!fr.sphere(e.center, e.radius)) continue;
    PackDrawCmd c;
    c.geometry = d.geometry;
    c.material = d.material;
    c.firstInstance = static_cast<std::uint32_t>(e.draw);
    c.mirrored = e.mirrored;
    c.renderOrder = d.renderOrder;
    c.viewDepth = depthOf(e.center);
    out.draws.push_back(c);
  }
  for (const InstancedEntry& e : instanced_) {
    const PackInstanced& in = pack_.instanced[static_cast<std::size_t>(e.index)];
    if (in.map == 1 && !turbNear) continue;
    if (!fr.sphere(e.center, e.radius)) continue;
    PackDrawCmd c;
    c.geometry = in.geometry;
    c.material = in.material;
    c.firstInstance = e.first;
    c.instanceCount = in.count;
    c.instanced = true;
    c.tint = in.colors ? Tint::Color : Tint::None;
    c.viewDepth = depthOf(e.center);
    out.draws.push_back(c);
  }

  // vegetação com LOD por instância: uma faixa de instâncias por (campo, nível)
  updateLod(c2);
  for (const LodFieldState& f : fields_) {
    if (f.src->map == 1 && !turbNear) continue;
    for (std::size_t l = 0; l < f.src->levels.size(); ++l) {
      const auto first = static_cast<std::uint32_t>(out.instances.size());
      for (const LodChunk& c : f.chunks) {
        if (c.mode == -1) continue;
        if (!fr.sphere(c.center, c.sphereRadius)) continue;
        for (std::uint32_t i = 0; i < c.n; ++i)
          if (c.level[i] == l) out.instances.push_back(c.items[i]);
      }
      const auto count = static_cast<std::uint32_t>(out.instances.size()) - first;
      if (!count) continue;
      out.lodInstances += count;
      for (const PackLodPart& p : f.src->levels[l]) {
        PackDrawCmd c;
        c.geometry = p.geometry;
        c.material = p.material;
        c.firstInstance = first;
        c.instanceCount = count;
        c.frameInstances = true;
        c.instanced = true;
        c.tint = p.tint == PackLodPart::Tint::Foliage ? Tint::Foliage : p.tint == PackLodPart::Tint::Trunk ? Tint::Trunk : Tint::None;
        out.draws.push_back(c);
      }
    }
  }

  // modelos dinâmicos do cenário
  // turbulent.js: o cristal gira (rotation.y += dt) e flutua (2,7 + sen(2t + x)·0,15)
  const auto crystal = [&](const PackModel& m, const glm::mat4& local) {
    const auto t = static_cast<float>(dyn.time);
    glm::mat4 r = glm::mat4(glm::mat3(local));
    const float c = std::cos(t), s = std::sin(t);
    const glm::mat4 ry(glm::vec4(c, 0, -s, 0), glm::vec4(0, 1, 0, 0), glm::vec4(s, 0, c, 0), glm::vec4(0, 0, 0, 1));
    r = ry * r;
    r[3] = glm::vec4(local[3].x, 2.7f + std::sin(t * 2.0f + static_cast<float>(m.x)) * 0.15f, local[3].z, 1.0f);
    return r;
  };
  const auto model = [&](const PackModel& m, const glm::mat4& root, bool shrine, bool skipCrystal) {
    const glm::vec3 p(root[3]);
    if (std::hypot(p.x - cam.x, p.z - cam.z) > detail_.blockFar) return;
    for (const PackDraw& part : m.parts) {
      const bool isCrystal = shrine && part.name == "crystal";
      if (isCrystal && skipCrystal) continue;
      const glm::mat4 w = root * (isCrystal ? crystal(m, part.matrix) : part.matrix);
      glm::vec3 c;
      float r = 0;
      boundsOf(meshes_.geometries[static_cast<std::size_t>(part.geometry)], w, c, r);
      if (!fr.sphere(c, r)) continue;
      PackDrawCmd d;
      d.geometry = part.geometry;
      d.material = part.material;
      d.firstInstance = static_cast<std::uint32_t>(out.instances.size());
      d.frameInstances = true;
      d.mirrored = glm::determinant(glm::mat3(w)) < 0.0f;
      d.viewDepth = depthOf(c);
      out.instances.push_back(makeInstance(w));
      out.draws.push_back(d);
    }
  };
  for (std::size_t i = 0; i < pack_.nodes.size(); ++i) {
    const bool depleted = i < dyn.nodeDepleted.size() && dyn.nodeDepleted[i];
    model(pack_.nodes[i], depleted ? pack_.nodes[i].matrix * glm::mat4(glm::mat3(0.55f)) : pack_.nodes[i].matrix, false, false);
  }
  for (std::size_t i = 0; i < pack_.shrines.size(); ++i)
    model(pack_.shrines[i], pack_.shrines[i].matrix, true, i < dyn.shrineTaken.size() && dyn.shrineTaken[i]);
  if (pack_.campPalisade && dyn.campPalisade) model(*pack_.campPalisade, pack_.campPalisade->matrix, false, false);

  // modelos das entidades (portal, sacos, projéteis, armas)
  for (const ModelInstance& mi : dyn.models) {
    if (!mi.parts) {
      if (mi.geometry < 0) continue;
      glm::vec3 c;
      float r = 0;
      boundsOf(meshes_.geometries[static_cast<std::size_t>(mi.geometry)], mi.root, c, r);
      if (!fr.sphere(c, r)) continue;
      PackDrawCmd d;
      d.geometry = mi.geometry;
      d.material = mi.material;
      d.firstInstance = static_cast<std::uint32_t>(out.instances.size());
      d.frameInstances = true;
      d.mirrored = glm::determinant(glm::mat3(mi.root)) < 0.0f;
      d.viewDepth = depthOf(c);
      Instance inst = makeInstance(mi.root);
      if (mi.color) {
        inst.tint[0] = mi.color->r;
        inst.tint[1] = mi.color->g;
        inst.tint[2] = mi.color->b;
        inst.tint[3] = mi.color->a;
        d.tint = Tint::Color;
      }
      out.instances.push_back(inst);
      out.draws.push_back(d);
      continue;
    }
    int shard = 0;
    for (const PackDraw& part : *mi.parts) {
      glm::mat4 local = part.matrix;
      Instance tint{};
      bool tinted = false;
      if (mi.portal && part.name == "shard") {
        // a = i/5·2π + 1,3t; posição (cos a·3, 3 + sen 2a·0,6, sen a·0,6); gira 3 rad/s
        const auto t = static_cast<float>(dyn.time);
        const float a = static_cast<float>(shard++) / 5.0f * 6.2831853f + t * 1.3f;
        const float c = std::cos(t * 3.0f), sn = std::sin(t * 3.0f);
        local = glm::mat4(glm::vec4(c, 0, -sn, 0), glm::vec4(0, 1, 0, 0), glm::vec4(sn, 0, c, 0),
                          glm::vec4(std::cos(a) * 3.0f, 3.0f + std::sin(a * 2.0f) * 0.6f, std::sin(a) * 0.6f, 1.0f)) *
                glm::mat4(glm::mat3(part.matrix));
      } else if (mi.portal && part.name == "disc") {
        // opacidade 0,45 + sen(3t)·0,15 sobre os 0,6 do material
        tinted = true;
        tint.tint[0] = tint.tint[1] = tint.tint[2] = 1.0f;
        tint.tint[3] = static_cast<float>((0.45 + std::sin(dyn.time * 3.0) * 0.15) / 0.6);
      }
      const glm::mat4 w = mi.root * local;
      glm::vec3 c;
      float r = 0;
      boundsOf(meshes_.geometries[static_cast<std::size_t>(part.geometry)], w, c, r);
      if (!fr.sphere(c, r)) continue;
      PackDrawCmd d;
      d.geometry = part.geometry;
      d.material = part.material;
      d.firstInstance = static_cast<std::uint32_t>(out.instances.size());
      d.frameInstances = true;
      d.mirrored = glm::determinant(glm::mat3(w)) < 0.0f;
      d.viewDepth = depthOf(c);
      d.renderOrder = part.renderOrder;
      Instance inst = makeInstance(w);
      if (tinted) {
        std::copy(std::begin(tint.tint), std::end(tint.tint), std::begin(inst.tint));
        d.tint = Tint::Color;
      }
      out.instances.push_back(inst);
      out.draws.push_back(d);
    }
  }

  // personagens: malhas com pele (a paleta do quadro vai junto)
  if (!dyn.skinned.empty()) {
    out.bones = dyn.bones;
    for (const SkinnedInstance& si : dyn.skinned) {
      if (!fr.sphere(si.center, si.radius)) continue;
      PackDrawCmd d;
      d.geometry = si.geometry;
      d.material = si.material;
      d.firstInstance = static_cast<std::uint32_t>(out.instances.size());
      d.frameInstances = true;
      d.viewDepth = depthOf(si.center);
      d.boneBase = static_cast<std::int32_t>(si.boneBase);
      Instance inst = makeInstance(si.mesh);
      if (si.color) {
        inst.tint[0] = si.color->r;
        inst.tint[1] = si.color->g;
        inst.tint[2] = si.color->b;
        inst.tint[3] = si.color->a;
        d.tint = Tint::Color;
      }
      out.instances.push_back(inst);
      out.draws.push_back(d);
    }
  }

  // ordem do three: opacos por renderOrder; transparentes por renderOrder e de trás para a frente
  const auto transparent = [&](const PackDrawCmd& d) { return pack_.materials[static_cast<std::size_t>(d.material)].transparent; };
  std::stable_sort(out.draws.begin(), out.draws.end(), [&](const PackDrawCmd& a, const PackDrawCmd& b) {
    const bool ta = transparent(a), tb = transparent(b);
    if (ta != tb) return !ta;
    if (a.renderOrder != b.renderOrder) return a.renderOrder < b.renderOrder;
    if (ta) return a.viewDepth > b.viewDepth;
    return a.material < b.material;
  });
}

}  // namespace rpg::client
