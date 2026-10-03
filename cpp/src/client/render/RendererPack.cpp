// Pacote visual na GPU: texturas (WebP → RGBA8, sRGB, arrays, mips), o buffer único de vértices e
// índices, as instâncias fixas, os materiais e os pipelines (criados sob demanda por estado).
#include <algorithm>
#include <cstring>

#include <glm/gtc/packing.hpp>

#include "client/assets/Image.h"
#include "client/render/DfgLut.h"
#include "client/render/RendererImpl.h"
#include "core/Log.h"
#include "core/Paths.h"

namespace rpg::client {

namespace {

SDL_GPUSamplerAddressMode addressMode(Wrap w) {
  switch (w) {
    case Wrap::Repeat: return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    case Wrap::Mirror: return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
    default: return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  }
}

// Níveis até o lado menor chegar a 1 (o blit do SDL não aceita um lado zerado).
int mipCount(int w, int h) {
  int n = 1;
  for (int m = std::min(w, h); m > 1; m >>= 1) ++n;
  return n;
}

}  // namespace

void Renderer::Impl::releasePack() {
  PackGpu& P = pack;
  if (!dev) return;
  for (SDL_GPUBuffer* b : {P.vb, P.ib, P.staticInst, P.skinVB, P.frameInst.buf, P.bones.buf})
    if (b) SDL_ReleaseGPUBuffer(dev, b);
  for (const auto& t : P.textures)
    if (t.tex) SDL_ReleaseGPUTexture(dev, t.tex);
  for (SDL_GPUTexture* t : {P.white, P.flatNormal, P.dfg, P.env, P.whiteArray})
    if (t) SDL_ReleaseGPUTexture(dev, t);
  for (const auto& [k, s] : P.samplers) SDL_ReleaseGPUSampler(dev, s);
  for (SDL_GPUSampler* s : {P.clampLinear, P.envSampler})
    if (s) SDL_ReleaseGPUSampler(dev, s);
  for (const auto& [k, p] : P.pipelines) SDL_ReleaseGPUGraphicsPipeline(dev, p);
  for (SDL_GPUShader* s : {P.meshV, P.skinnedV, P.surfaceF, P.terrainF})
    if (s) SDL_ReleaseGPUShader(dev, s);
  P = PackGpu{};
}

SDL_GPUSampler* Renderer::Impl::packSampler(Wrap s, Wrap t, bool nearest, bool mips, int anisotropy) {
  const auto key = std::make_tuple(static_cast<int>(s), static_cast<int>(t), nearest, mips, anisotropy);
  if (const auto it = pack.samplers.find(key); it != pack.samplers.end()) return it->second;
  SDL_GPUSamplerCreateInfo si{};
  si.min_filter = SDL_GPU_FILTER_LINEAR;
  si.mag_filter = nearest ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
  si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
  si.address_mode_u = addressMode(s);
  si.address_mode_v = addressMode(t);
  si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  si.min_lod = 0;
  si.max_lod = mips ? 1000.0f : 0.0f;
  if (mips && anisotropy > 1) {
    si.enable_anisotropy = true;
    si.max_anisotropy = static_cast<float>(anisotropy);
  }
  SDL_GPUSampler* smp = SDL_CreateGPUSampler(dev, &si);
  if (!smp) gpuFail("SDL_CreateGPUSampler (pacote)");
  pack.samplers.emplace(key, smp);
  return smp;
}

SDL_GPUTexture* Renderer::Impl::uploadTexture(SDL_GPUTextureType type, SDL_GPUTextureFormat fmt, int w, int h, int layers, int levels,
                                              const std::vector<std::vector<const std::uint8_t*>>& data, int bpp, bool generateMips) {
  SDL_GPUTextureCreateInfo ci{};
  ci.type = type;
  ci.format = fmt;
  ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | (generateMips && levels > 1 ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0);
  ci.width = static_cast<Uint32>(w);
  ci.height = static_cast<Uint32>(h);
  ci.layer_count_or_depth = static_cast<Uint32>(layers);
  ci.num_levels = static_cast<Uint32>(levels);
  SDL_GPUTexture* tex = SDL_CreateGPUTexture(dev, &ci);
  if (!tex) gpuFail("SDL_CreateGPUTexture (pacote)");
  std::size_t total = 0;
  for (std::size_t l = 0; l < data.size(); ++l)
    total += static_cast<std::size_t>(std::max(1, w >> l)) * static_cast<std::size_t>(std::max(1, h >> l)) * static_cast<std::size_t>(bpp) * data[l].size();
  SDL_GPUTransferBufferCreateInfo ti{};
  ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  ti.size = static_cast<Uint32>(total);
  SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &ti);
  if (!tb) gpuFail("SDL_CreateGPUTransferBuffer (textura)");
  auto* dst = static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(dev, tb, false));
  std::size_t off = 0;
  for (std::size_t l = 0; l < data.size(); ++l) {
    const std::size_t bytes = static_cast<std::size_t>(std::max(1, w >> l)) * static_cast<std::size_t>(std::max(1, h >> l)) * static_cast<std::size_t>(bpp);
    for (const std::uint8_t* px : data[l]) {
      std::memcpy(dst + off, px, bytes);
      off += bytes;
    }
  }
  SDL_UnmapGPUTransferBuffer(dev, tb);
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
  SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
  off = 0;
  for (std::size_t l = 0; l < data.size(); ++l) {
    const int lw = std::max(1, w >> l), lh = std::max(1, h >> l);
    for (std::size_t layer = 0; layer < data[l].size(); ++layer) {
      SDL_GPUTextureTransferInfo src{tb, static_cast<Uint32>(off), static_cast<Uint32>(lw), static_cast<Uint32>(lh)};
      SDL_GPUTextureRegion reg{};
      reg.texture = tex;
      reg.mip_level = static_cast<Uint32>(l);
      reg.layer = static_cast<Uint32>(layer);
      reg.w = static_cast<Uint32>(lw);
      reg.h = static_cast<Uint32>(lh);
      reg.d = 1;
      SDL_UploadToGPUTexture(cp, &src, &reg, false);
      off += static_cast<std::size_t>(lw) * static_cast<std::size_t>(lh) * static_cast<std::size_t>(bpp);
    }
  }
  SDL_EndGPUCopyPass(cp);
  if (generateMips && levels > 1) SDL_GenerateMipmapsForGPUTexture(cmd, tex);
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_ReleaseGPUTransferBuffer(dev, tb);
  return tex;
}

SDL_GPUGraphicsPipeline* Renderer::Impl::packPipeline(const MaterialSetup& m, bool mirrored, bool skinned) {
  const PackGpu::PipelineKey key{m.terrain, static_cast<int>(m.cull), static_cast<int>(m.blend), m.depthWrite, m.depthTest,
                                 m.depthBias, m.biasFactor, m.biasUnits, mirrored, skinned};
  if (const auto it = pack.pipelines.find(key); it != pack.pipelines.end()) return it->second;
  // pele (skinned.vert): o terceiro fluxo traz os índices dos ossos e os pesos
  const SDL_GPUVertexBufferDescription bufs[3] = {
      {0, sizeof(PackVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0},
      {1, sizeof(Instance), SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0},
      {2, sizeof(SkinVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0},
  };
  const SDL_GPUVertexAttribute attrs[15] = {
      {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
      {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 24},       {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_HALF4, 32},
      {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 40},  {5, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 44},
      {6, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 48},  {7, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, 52},
      {8, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0},        {9, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 16},
      {10, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 32},      {11, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 48},
      {12, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 64},
      {13, 2, SDL_GPU_VERTEXELEMENTFORMAT_USHORT4, 0},      {14, 2, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 8},
  };
  SDL_GPUColorTargetDescription ct{};
  ct.format = kHdrFmt;
  if (m.blend != Blend::Opaque) {
    // NormalBlending / AdditiveBlending do three (alfa não pré-multiplicado)
    ct.blend_state.enable_blend = true;
    ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    ct.blend_state.dst_color_blendfactor = m.blend == Blend::Additive ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    ct.blend_state.src_alpha_blendfactor = m.blend == Blend::Additive ? SDL_GPU_BLENDFACTOR_SRC_ALPHA : SDL_GPU_BLENDFACTOR_ONE;
    ct.blend_state.dst_alpha_blendfactor = m.blend == Blend::Additive ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  }
  SDL_GPUGraphicsPipelineCreateInfo pi{};
  pi.vertex_shader = skinned ? pack.skinnedV : pack.meshV;
  pi.fragment_shader = m.terrain ? pack.terrainF : pack.surfaceF;
  pi.vertex_input_state = {bufs, skinned ? 3u : 2u, attrs, skinned ? 15u : 13u};
  pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  pi.rasterizer_state.cull_mode = m.cull == Cull::None ? SDL_GPU_CULLMODE_NONE : m.cull == Cull::Back ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_FRONT;
  pi.rasterizer_state.front_face = mirrored ? SDL_GPU_FRONTFACE_CLOCKWISE : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  pi.rasterizer_state.enable_depth_clip = true;
  if (m.depthBias) {
    pi.rasterizer_state.enable_depth_bias = true;
    pi.rasterizer_state.depth_bias_slope_factor = m.biasFactor;
    pi.rasterizer_state.depth_bias_constant_factor = m.biasUnits;
  }
  pi.depth_stencil_state.enable_depth_test = m.depthTest;
  pi.depth_stencil_state.enable_depth_write = m.depthWrite;
  pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
  pi.target_info.color_target_descriptions = &ct;
  pi.target_info.num_color_targets = 1;
  pi.target_info.has_depth_stencil_target = true;
  pi.target_info.depth_stencil_format = depthFmt;
  SDL_GPUGraphicsPipeline* p = SDL_CreateGPUGraphicsPipeline(dev, &pi);
  if (!p) gpuFail("SDL_CreateGPUGraphicsPipeline (pacote)");
  pack.pipelines.emplace(key, p);
  return p;
}

bool Renderer::packLoaded() const { return impl_->pack.loaded; }

void Renderer::loadPack(const ScenePack& sp, const PackMeshes& meshes, const std::vector<Instance>& staticInstances,
                        const std::function<void(double)>& progress) {
  Impl& I = *impl_;
  I.releasePack();
  PackGpu& P = I.pack;
  P.meshV = I.shader("mesh.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 2);
  P.skinnedV = I.shader("skinned.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 2, 1);
  P.surfaceF = I.shader("surface.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 7, 2);
  P.terrainF = I.shader("terrain.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 5, 2);

  P.vb = I.staticBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, meshes.vertices.data(), static_cast<Uint32>(meshes.vertices.size() * sizeof(PackVertex)));
  P.ib = I.staticBuffer(SDL_GPU_BUFFERUSAGE_INDEX, meshes.indices.data(), static_cast<Uint32>(meshes.indices.size() * sizeof(std::uint32_t)));
  P.skinVB = I.staticBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, meshes.skin.data(), static_cast<Uint32>(meshes.skin.size() * sizeof(SkinVertex)));
  P.staticInst = I.staticBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, staticInstances.data(), static_cast<Uint32>(staticInstances.size() * sizeof(Instance)));
  P.geometries = meshes.geometries;
  P.terrain = meshes.terrain;

  // texturas padrão dos slots vazios e a tabela DFG
  const std::uint8_t white[4] = {255, 255, 255, 255}, flat[4] = {128, 128, 255, 255};
  P.white = I.uploadTexture(SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 1, 1, 1, 1, {{white}}, 4, false);
  P.flatNormal = I.uploadTexture(SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 1, 1, 1, 1, {{flat}}, 4, false);
  P.whiteArray = I.uploadTexture(SDL_GPU_TEXTURETYPE_2D_ARRAY, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 1, 1, 1, 1, {{white}}, 4, false);
  P.dfg = I.uploadTexture(SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT, kDfgLutSize, kDfgLutSize, 1, 1,
                          {{reinterpret_cast<const std::uint8_t*>(kDfgLut.data())}}, 4, false);
  const std::uint16_t black[4] = {0, 0, 0, 0x3c00};
  P.env = I.uploadTexture(SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, 1, 1, 1, 1,
                          {{reinterpret_cast<const std::uint8_t*>(black)}}, 8, false);
  {
    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = si.mag_filter = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    P.clampLinear = SDL_CreateGPUSampler(I.dev, &si);
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    si.max_lod = 1000.0f;
    P.envSampler = SDL_CreateGPUSampler(I.dev, &si);
    if (!P.clampLinear || !P.envSampler) gpuFail("SDL_CreateGPUSampler (ambiente)");
  }

  // texturas do pacote (uma imagem decodificada pode servir a várias texturas)
  std::map<std::string, ImageRGBA> decoded;
  const auto image = [&](const std::string& file) -> const ImageRGBA& {
    auto it = decoded.find(file);
    if (it == decoded.end()) it = decoded.emplace(file, loadWebp(sp.dir / pathFromUtf8(file))).first;
    return it->second;
  };
  P.textures.resize(sp.textures.size());
  for (std::size_t i = 0; i < sp.textures.size(); ++i) {
    const PackTexture& t = sp.textures[i];
    const PackSource& src = sp.sources[static_cast<std::size_t>(t.source)];
    std::vector<ImageRGBA> layers;
    for (const std::string& f : src.files) {
      layers.push_back(image(f));
      if (t.flipY) layers.back().flipRows();
    }
    std::vector<ImageRGBA> mips;
    for (const PackMip& m : t.mipLevels) {
      mips.push_back(image(m.file));
      if (t.flipY) mips.back().flipRows();
    }
    std::vector<std::vector<const std::uint8_t*>> data(1);
    for (const ImageRGBA& l : layers) data[0].push_back(l.pixels.data());
    for (const ImageRGBA& m : mips) data.push_back({m.pixels.data()});
    const bool provided = !mips.empty();
    const int levels = provided ? static_cast<int>(data.size()) : t.mipmaps ? mipCount(src.w, src.h) : 1;
    PackGpu::Tex& g = P.textures[i];
    g.array = t.array;
    g.tex = I.uploadTexture(t.array ? SDL_GPU_TEXTURETYPE_2D_ARRAY : SDL_GPU_TEXTURETYPE_2D,
                            t.srgb ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, src.w, src.h,
                            static_cast<int>(layers.size()), levels, data, 4, !provided && levels > 1);
    g.sampler = I.packSampler(t.wrapS, t.wrapT, t.nearest, levels > 1, t.anisotropy);
    if (progress) progress(static_cast<double>(i + 1) / static_cast<double>(sp.textures.size()));
  }

  // materiais
  for (const PackMaterial& pm : sp.materials) {
    PackGpu::Mat m;
    m.setup = setupMaterial(sp, pm);
    const auto bind = [&](std::size_t slot, int texId, bool wantArray, SDL_GPUTexture* fallback) {
      SDL_GPUTextureSamplerBinding b{fallback, P.clampLinear};
      if (texId >= 0 && static_cast<std::size_t>(texId) < P.textures.size() && P.textures[static_cast<std::size_t>(texId)].array == wantArray)
        b = {P.textures[static_cast<std::size_t>(texId)].tex, P.textures[static_cast<std::size_t>(texId)].sampler};
      m.bindings[slot] = b;
    };
    if (m.setup.terrain) {
      bind(0, m.setup.textures[0], true, P.whiteArray);
      bind(1, m.setup.textures[1], true, P.whiteArray);
      bind(2, m.setup.textures[2], false, P.white);
      m.bindingCount = 5;  // 3 = ambiente, 4 = DFG (no desenho)
    } else {
      for (std::size_t s = 0; s < kSurfaceSlots; ++s) bind(s, m.setup.textures[s], false, s == 1 ? P.flatNormal : P.white);
      m.bindingCount = 7;  // 5 = ambiente, 6 = DFG
    }
    P.materials.push_back(m);
  }
  P.loaded = true;
  log::info("pacote visual: {} vértices, {} índices, {} texturas, {} materiais", meshes.vertices.size(), meshes.indices.size(),
            sp.textures.size(), sp.materials.size());
}

void Renderer::setEnvironment(const EnvironmentMap* env) {
  Impl& I = *impl_;
  PackGpu& P = I.pack;
  if (!P.loaded) return;
  if (P.env) SDL_ReleaseGPUTexture(I.dev, P.env);
  if (!env || env->w <= 0) {
    const std::uint16_t black[4] = {0, 0, 0, 0x3c00};
    P.env = I.uploadTexture(SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, 1, 1, 1, 1,
                            {{reinterpret_cast<const std::uint8_t*>(black)}}, 8, false);
    return;
  }
  std::vector<std::uint16_t> half(static_cast<std::size_t>(env->w) * static_cast<std::size_t>(env->h) * 4);
  for (std::size_t i = 0, n = static_cast<std::size_t>(env->w) * static_cast<std::size_t>(env->h); i < n; ++i) {
    for (std::size_t k = 0; k < 3; ++k) half[i * 4 + k] = glm::packHalf1x16(env->rgb[i * 3 + k]);
    half[i * 4 + 3] = 0x3c00;
  }
  P.env = I.uploadTexture(SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, env->w, env->h, 1, mipCount(env->w, env->h),
                          {{reinterpret_cast<const std::uint8_t*>(half.data())}}, 8, true);
}

void Renderer::Impl::drawPack(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* rp, const PackFrame& f, bool transparent, FrameStats& st) {
  PackGpu& P = pack;
  SDL_GPUGraphicsPipeline* bound = nullptr;
  int boundMat = -1;
  SDL_GPUBuffer* boundInst = nullptr;
  Uint32 boundInstOffset = ~0u;
  const SDL_GPUBufferBinding ib{P.ib, 0};
  SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
  bool bonesBound = false;
  const auto useMaterial = [&](int mi, bool mirrored, bool instanced, Tint tint, std::int32_t boneBase = -1) {
    const PackGpu::Mat& m = P.materials[static_cast<std::size_t>(mi)];
    SDL_GPUGraphicsPipeline* p = packPipeline(m.setup, mirrored, boneBase >= 0);
    if (p != bound) {
      SDL_BindGPUGraphicsPipeline(rp, p);
      bound = p;
      boundMat = -1;
      boundInst = nullptr;
    }
    if (boneBase >= 0 && !bonesBound) {
      SDL_BindGPUVertexStorageBuffers(rp, 0, &P.bones.buf, 1);
      bonesBound = true;
    }
    DrawBlock d = m.setup.draw;
    d.wind[2] = instanced ? 1.0f : 0.0f;
    d.wind[3] = static_cast<float>(tint);
    if (boneBase >= 0) {
      d.wind[1] = 0.0f;  // sem vento nem grama
      d.flags[1] = static_cast<float>(boneBase);
    }
    SDL_PushGPUVertexUniformData(cmd, 1, &d, sizeof d);
    if (mi != boundMat) {
      SDL_PushGPUFragmentUniformData(cmd, 1, &m.setup.block, sizeof m.setup.block);
      std::array<SDL_GPUTextureSamplerBinding, 7> b = m.bindings;
      b[m.bindingCount - 2] = {P.env, P.envSampler};
      b[m.bindingCount - 1] = {P.dfg, P.clampLinear};
      SDL_BindGPUFragmentSamplers(rp, 0, b.data(), m.bindingCount);
      boundMat = mi;
    }
  };
  const auto bindVertex = [&](SDL_GPUBuffer* inst, Uint32 offset) {
    if (inst == boundInst && offset == boundInstOffset) return;
    const SDL_GPUBufferBinding vb[2] = {{P.vb, 0}, {inst, offset}};
    SDL_BindGPUVertexBuffers(rp, 0, vb, 2);
    boundInst = inst;
    boundInstOffset = offset;
  };

  if (!transparent) {
    for (const auto& [tile, lod] : f.terrain) {
      const TerrainSlice& t = P.terrain[static_cast<std::size_t>(tile)];
      const MeshSlice& s = t.lod[static_cast<std::size_t>(lod)];
      useMaterial(t.material, false, false, Tint::None);
      bindVertex(identity, 0);
      SDL_DrawGPUIndexedPrimitives(rp, s.indexCount, 1, s.firstIndex, s.baseVertex, 0);
      ++st.drawCalls;
      ++st.chunksDrawn;
      st.triangles += s.indexCount / 3;
    }
  }
  for (const PackDrawCmd& d : f.draws) {
    const PackGpu::Mat& m = P.materials[static_cast<std::size_t>(d.material)];
    if (m.setup.transparent != transparent) continue;
    const MeshSlice& s = P.geometries[static_cast<std::size_t>(d.geometry)];
    if (d.boneBase >= 0) {
      if (s.skinBase < 0 || !P.bones.buf) continue;
      useMaterial(d.material, d.mirrored, false, d.tint, d.boneBase);
      // pele: cada fluxo começa no primeiro vértice da geometria (índices locais, sem baseVertex)
      const SDL_GPUBufferBinding vb[3] = {
          {P.vb, static_cast<Uint32>(s.baseVertex) * static_cast<Uint32>(sizeof(PackVertex))},
          {P.frameInst.buf, static_cast<Uint32>(d.firstInstance * sizeof(Instance))},
          {P.skinVB, static_cast<Uint32>(s.skinBase) * static_cast<Uint32>(sizeof(SkinVertex))},
      };
      SDL_BindGPUVertexBuffers(rp, 0, vb, 3);
      boundInst = nullptr;
      SDL_DrawGPUIndexedPrimitives(rp, s.indexCount, 1, s.firstIndex, 0, 0);
      ++st.drawCalls;
      ++st.instances;
      st.triangles += s.indexCount / 3;
      continue;
    }
    useMaterial(d.material, d.mirrored, d.instanced, d.tint);
    bindVertex(d.frameInstances ? P.frameInst.buf : P.staticInst, static_cast<Uint32>(d.firstInstance * sizeof(Instance)));
    SDL_DrawGPUIndexedPrimitives(rp, s.indexCount, d.instanceCount, s.firstIndex, s.baseVertex, 0);
    ++st.drawCalls;
    st.instances += d.instanceCount;
    st.triangles += static_cast<std::uint64_t>(s.indexCount / 3) * d.instanceCount;
  }
}

}  // namespace rpg::client
