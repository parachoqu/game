// Pós-processamento (engine/renderer.js `setupPost`): GTAO, bloom e anti-serrilhado, conforme o nível.
// A ordem é a do EffectComposer da demo: cena → GTAO → bloom → saída (ACES + sRGB) → anti-serrilhado.
// A oclusão e o bloom entram na composição (composite.frag); o FXAA lê a saída e grava a final.
#include <algorithm>
#include <cmath>

#include <glm/gtc/type_ptr.hpp>

#include "client/render/RendererImpl.h"

namespace rpg::client {

namespace {

struct AoBlock {
  float proj[16], invProj[16], view[16];
  float params[4];  // raio, expoente de distância, espessura, escala
  float size[4];
};
struct AoBlurBlock {
  float params[4];  // raio (px), lumaPhi, depthPhi, normalPhi
  float size[4];
};
struct BrightBlock {
  float params[4];  // limiar, transição, teto, mistura da oclusão
};
struct BlurBlock {
  float dir[4];  // xy: direção / tamanho; z: raio do kernel
  float coeff[24];
};
struct SizeBlock {
  float size[4];
};

}  // namespace

void Renderer::Impl::initPost() {
  if (post.ready) return;
  PostGpu& P = post;
  P.fullV = shader("fullscreen.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
  const SDL_GPUVertexInputState none{nullptr, 0, nullptr, 0};
  const auto make = [&](const char* frag, Uint32 samplers, SDL_GPUTextureFormat fmt) {
    SDL_GPUShader* f = shader(frag, SDL_GPU_SHADERSTAGE_FRAGMENT, samplers, 1);
    SDL_GPUGraphicsPipeline* p = pipeline(P.fullV, f, none, fmt, false, false, false, false);
    SDL_ReleaseGPUShader(dev, f);
    return p;
  };
  P.gtao = make("gtao.frag", 2, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
  P.aoBlur = make("aoblur.frag", 3, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
  P.brightPass = make("bloom_bright.frag", 2, kHdrFmt);
  P.blur = make("bloom_blur.frag", 1, kHdrFmt);
  P.fxaa = make("fxaa.frag", 1, kOutFmt);
  SDL_GPUSamplerCreateInfo si{};
  si.min_filter = si.mag_filter = SDL_GPU_FILTER_NEAREST;
  si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  P.point = SDL_CreateGPUSampler(dev, &si);
  si.min_filter = si.mag_filter = SDL_GPU_FILTER_LINEAR;
  P.clamp = SDL_CreateGPUSampler(dev, &si);
  if (!P.point || !P.clamp) gpuFail("SDL_CreateGPUSampler (pós)");
  // texturas neutras: oclusão 1 (branco) e bloom 0 (preto)
  P.white = texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, 4, 4);
  P.black = texture(kHdrFmt, SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, 4, 4);
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
  for (const auto& [t, c] : {std::pair{P.white, SDL_FColor{1, 1, 1, 1}}, std::pair{P.black, SDL_FColor{0, 0, 0, 0}}}) {
    SDL_GPUColorTargetInfo ct{};
    ct.texture = t;
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    ct.clear_color = c;
    SDL_EndGPURenderPass(SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr));
  }
  SDL_SubmitGPUCommandBuffer(cmd);
  P.ready = true;
}

void Renderer::Impl::releasePost() {
  PostGpu& P = post;
  if (!dev || !P.ready) return;
  for (SDL_GPUGraphicsPipeline* p : {P.gtao, P.aoBlur, P.brightPass, P.blur, P.fxaa})
    if (p) SDL_ReleaseGPUGraphicsPipeline(dev, p);
  for (SDL_GPUSampler* s : {P.point, P.clamp})
    if (s) SDL_ReleaseGPUSampler(dev, s);
  if (P.fullV) SDL_ReleaseGPUShader(dev, P.fullV);
  std::vector<SDL_GPUTexture*> tex = {P.aoNormal, P.aoDepth, P.ao, P.aoTmp, P.brightTex, P.ldr, P.sceneCache, P.white, P.black};
  tex.insert(tex.end(), P.bloomH.begin(), P.bloomH.end());
  tex.insert(tex.end(), P.bloomV.begin(), P.bloomV.end());
  for (SDL_GPUTexture* t : tex)
    if (t) SDL_ReleaseGPUTexture(dev, t);
  P = PostGpu{};
}

void Renderer::Impl::ensurePostTargets(int w, int h) {
  initPost();
  PostGpu& P = post;
  if (P.w == w && P.h == h && P.ldr) return;
  std::vector<SDL_GPUTexture*> old = {P.aoNormal, P.aoDepth, P.ao, P.aoTmp, P.brightTex, P.ldr, P.sceneCache};
  old.insert(old.end(), P.bloomH.begin(), P.bloomH.end());
  old.insert(old.end(), P.bloomV.begin(), P.bloomV.end());
  for (SDL_GPUTexture* t : old)
    if (t) SDL_ReleaseGPUTexture(dev, t);
  const SDL_GPUTextureUsageFlags rt = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
  // GTAO e bloom em meia resolução (GTAOPass.setSize e UnrealBloomPass: Math.round(w / 2))
  const int hw = std::max(1, (w + 1) / 2), hh = std::max(1, (h + 1) / 2);
  P.aoNormal = texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, rt, hw, hh);
  P.aoDepth = texture(depthFmt, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, hw, hh);
  P.ao = texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, rt, hw, hh);
  P.aoTmp = texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, rt, hw, hh);
  P.brightTex = texture(kHdrFmt, rt, hw, hh);
  int bw = hw, bh = hh;
  for (std::size_t i = 0; i < 5; ++i) {
    P.bloomW[i] = bw;
    P.bloomHgt[i] = bh;
    P.bloomH[i] = texture(kHdrFmt, rt, bw, bh);
    P.bloomV[i] = texture(kHdrFmt, rt, bw, bh);
    bw = std::max(1, (bw + 1) / 2);  // Math.round(resx / 2)
    bh = std::max(1, (bh + 1) / 2);
  }
  P.ldr = texture(kOutFmt, rt, w, h);
  P.sceneCache = texture(kOutFmt, rt, w, h);
  P.sceneCached = false;
  P.w = w;
  P.h = h;
}

void Renderer::Impl::fullscreen(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, SDL_GPUGraphicsPipeline* p,
                                std::initializer_list<SDL_GPUTextureSamplerBinding> tex, const void* uniforms, Uint32 size,
                                FrameStats& st) {
  SDL_GPUColorTargetInfo ct{};
  ct.texture = target;
  ct.load_op = SDL_GPU_LOADOP_DONT_CARE;
  ct.store_op = SDL_GPU_STOREOP_STORE;
  SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
  SDL_BindGPUGraphicsPipeline(rp, p);
  if (uniforms) SDL_PushGPUFragmentUniformData(cmd, 0, uniforms, size);
  SDL_BindGPUFragmentSamplers(rp, 0, tex.begin(), static_cast<Uint32>(tex.size()));
  SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
  SDL_EndGPURenderPass(rp);
  ++st.drawCalls;
}

// Normais da oclusão: as mesmas entradas das malhas do pacote, só com a normal de saída.
SDL_GPUGraphicsPipeline* Renderer::Impl::aoPipeline(Cull cull, bool mirrored, bool skinned) {
  const auto key = std::make_tuple(static_cast<int>(cull), mirrored, skinned);
  if (const auto it = pack.aoPipelines.find(key); it != pack.aoPipelines.end()) return it->second;
  if (!pack.aoNormalF) pack.aoNormalF = shader("aonormal.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
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
  ct.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  SDL_GPUGraphicsPipelineCreateInfo pi{};
  pi.vertex_shader = skinned ? pack.skinnedV : pack.meshV;
  pi.fragment_shader = pack.aoNormalF;
  pi.vertex_input_state = {bufs, skinned ? 3u : 2u, attrs, skinned ? 15u : 13u};
  pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
  pi.rasterizer_state.cull_mode = cull == Cull::None ? SDL_GPU_CULLMODE_NONE : cull == Cull::Back ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_FRONT;
  pi.rasterizer_state.front_face = mirrored ? SDL_GPU_FRONTFACE_CLOCKWISE : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  pi.rasterizer_state.enable_depth_clip = true;
  pi.depth_stencil_state.enable_depth_test = true;
  pi.depth_stencil_state.enable_depth_write = true;
  pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
  pi.target_info.color_target_descriptions = &ct;
  pi.target_info.num_color_targets = 1;
  pi.target_info.has_depth_stencil_target = true;
  pi.target_info.depth_stencil_format = depthFmt;
  SDL_GPUGraphicsPipeline* p = SDL_CreateGPUGraphicsPipeline(dev, &pi);
  if (!p) gpuFail("SDL_CreateGPUGraphicsPipeline (normais da oclusão)");
  pack.aoPipelines.emplace(key, p);
  return p;
}

void Renderer::Impl::drawAo(SDL_GPUCommandBuffer* cmd, const Renderer::Frame& f, FrameStats& st) {
  PackGpu& Pk = pack;
  PostGpu& P = post;
  // ---- normais e profundidade das malhas opacas (relevo, construções, personagens)
  SDL_GPUColorTargetInfo ct{};
  ct.texture = P.aoNormal;
  ct.load_op = SDL_GPU_LOADOP_CLEAR;
  ct.store_op = SDL_GPU_STOREOP_STORE;
  ct.clear_color = {0, 0, 0, 0};
  SDL_GPUDepthStencilTargetInfo dt{};
  dt.texture = P.aoDepth;
  dt.clear_depth = 1.0f;
  dt.load_op = SDL_GPU_LOADOP_CLEAR;
  dt.store_op = SDL_GPU_STOREOP_STORE;
  dt.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
  dt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
  SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);
  SDL_PushGPUVertexUniformData(cmd, 0, &f.uniforms, sizeof f.uniforms);
  const SDL_GPUBufferBinding ib{Pk.ib, 0};
  SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
  SDL_GPUGraphicsPipeline* bound = nullptr;
  bool bonesBound = false;
  const auto use = [&](const PackGpu::Mat& m, bool mirrored, bool skinned, bool instanced, Tint tint, std::int32_t boneBase) {
    SDL_GPUGraphicsPipeline* p = aoPipeline(m.setup.cull, mirrored, skinned);
    if (p != bound) {
      SDL_BindGPUGraphicsPipeline(rp, p);
      bound = p;
    }
    if (skinned && !bonesBound) {
      SDL_BindGPUVertexStorageBuffers(rp, 0, &Pk.bones.buf, 1);
      bonesBound = true;
    }
    DrawBlock d = m.setup.draw;
    d.wind[2] = instanced ? 1.0f : 0.0f;
    d.wind[3] = static_cast<float>(tint);
    if (skinned) {
      d.wind[1] = 0.0f;
      d.flags[1] = static_cast<float>(boneBase);
    }
    SDL_PushGPUVertexUniformData(cmd, 1, &d, sizeof d);
  };
  const auto excluded = [](const PackGpu::Mat& m) { return m.setup.transparent || m.setup.block.pbr[2] > 0.0f; };
  for (const auto& [tile, lod] : f.pack->terrain) {
    const TerrainSlice& t = Pk.terrain[static_cast<std::size_t>(tile)];
    const MeshSlice& s = t.lod[static_cast<std::size_t>(lod)];
    use(Pk.materials[static_cast<std::size_t>(t.material)], false, false, false, Tint::None, -1);
    const SDL_GPUBufferBinding vb[2] = {{Pk.vb, 0}, {identity, 0}};
    SDL_BindGPUVertexBuffers(rp, 0, vb, 2);
    SDL_DrawGPUIndexedPrimitives(rp, s.indexCount, 1, s.firstIndex, s.baseVertex, 0);
    ++st.drawCalls;
  }
  for (const PackDrawCmd& d : f.pack->draws) {
    if (d.instanced) continue;  // InstancedMesh fica de fora
    const PackGpu::Mat& m = Pk.materials[static_cast<std::size_t>(d.material)];
    if (excluded(m)) continue;
    const MeshSlice& s = Pk.geometries[static_cast<std::size_t>(d.geometry)];
    const bool skinned = d.boneBase >= 0;
    if (skinned && (s.skinBase < 0 || !Pk.bones.buf)) continue;
    use(m, d.mirrored, skinned, false, d.tint, d.boneBase);
    SDL_GPUBuffer* inst = d.frameInstances || skinned ? Pk.frameInst.buf : Pk.staticInst;
    if (skinned) {
      const SDL_GPUBufferBinding vb[3] = {
          {Pk.vb, static_cast<Uint32>(s.baseVertex) * static_cast<Uint32>(sizeof(PackVertex))},
          {inst, static_cast<Uint32>(d.firstInstance * sizeof(Instance))},
          {Pk.skinVB, static_cast<Uint32>(s.skinBase) * static_cast<Uint32>(sizeof(SkinVertex))},
      };
      SDL_BindGPUVertexBuffers(rp, 0, vb, 3);
      SDL_DrawGPUIndexedPrimitives(rp, s.indexCount, 1, s.firstIndex, 0, 0);
    } else {
      const SDL_GPUBufferBinding vb[2] = {{Pk.vb, 0}, {inst, static_cast<Uint32>(d.firstInstance * sizeof(Instance))}};
      SDL_BindGPUVertexBuffers(rp, 0, vb, 2);
      SDL_DrawGPUIndexedPrimitives(rp, s.indexCount, d.instanceCount, s.firstIndex, s.baseVertex, 0);
    }
    ++st.drawCalls;
  }
  SDL_EndGPURenderPass(rp);

  // ---- oclusão (GTAOPass: raio 0,8, expoente 1,5, espessura 1,2, escala 1) e filtro de Poisson
  const auto hw = static_cast<float>(std::max(1, (texW + 1) / 2)), hh = static_cast<float>(std::max(1, (texH + 1) / 2));
  AoBlock ab{};
  std::memcpy(ab.proj, glm::value_ptr(f.proj), sizeof ab.proj);
  const glm::mat4 invProj = glm::inverse(f.proj);
  std::memcpy(ab.invProj, glm::value_ptr(invProj), sizeof ab.invProj);
  std::memcpy(ab.view, glm::value_ptr(f.view), sizeof ab.view);
  const float aoParams[4] = {0.8f, 1.5f, 1.2f, 1.0f};
  std::memcpy(ab.params, aoParams, sizeof ab.params);
  const float size[4] = {hw, hh, 1.0f / hw, 1.0f / hh};
  std::memcpy(ab.size, size, sizeof ab.size);
  fullscreen(cmd, P.aoTmp, P.gtao, {{P.aoDepth, P.point}, {P.aoNormal, P.point}}, &ab, sizeof ab, st);
  AoBlurBlock bb{};
  const float blurParams[4] = {5.0f, 10.0f, 2.0f, 3.0f};  // raio, lumaPhi, depthPhi, normalPhi
  std::memcpy(bb.params, blurParams, sizeof bb.params);
  std::memcpy(bb.size, size, sizeof bb.size);
  fullscreen(cmd, P.ao, P.aoBlur, {{P.aoTmp, P.clamp}, {P.aoDepth, P.point}, {P.aoNormal, P.point}}, &bb, sizeof bb, st);
}

// UnrealBloomPass(força 0,14, raio 0,4, limiar 1,15) do three r185: passa-alta em meia resolução e cinco
// níveis de desfoque separável (kernels 6, 10, 14, 18, 22 com sigma = raio / 3, three.js#31528), cada um
// com a metade do tamanho do anterior.
void Renderer::Impl::drawBloom(SDL_GPUCommandBuffer* cmd, FrameStats& st) {
  PostGpu& P = post;
  const bool ao = settings.gtao && pack.loaded;
  const BrightBlock br{{1.15f, 0.01f, 3.0f, ao ? 0.85f : 0.0f}};
  fullscreen(cmd, P.brightTex, P.brightPass, {{hdr, P.clamp}, {ao ? P.ao : P.white, P.clamp}}, &br, sizeof br, st);
  static constexpr int kKernel[5] = {6, 10, 14, 18, 22};
  SDL_GPUTexture* input = P.brightTex;
  for (std::size_t i = 0; i < 5; ++i) {
    BlurBlock bb{};
    const int radius = kKernel[i];
    const float sigma = static_cast<float>(radius) / 3.0f;
    for (int k = 0; k < radius; ++k)
      bb.coeff[k] = 0.39894f * std::exp(-0.5f * static_cast<float>(k * k) / (sigma * sigma)) / sigma;
    const float iw = 1.0f / static_cast<float>(P.bloomW[i]), ih = 1.0f / static_cast<float>(P.bloomHgt[i]);
    bb.dir[0] = iw;
    bb.dir[1] = 0;
    bb.dir[2] = static_cast<float>(radius);
    fullscreen(cmd, P.bloomH[i], P.blur, {{input, P.clamp}}, &bb, sizeof bb, st);
    bb.dir[0] = 0;
    bb.dir[1] = ih;
    fullscreen(cmd, P.bloomV[i], P.blur, {{P.bloomH[i], P.clamp}}, &bb, sizeof bb, st);
    input = P.bloomV[i];
  }
}

}  // namespace rpg::client
