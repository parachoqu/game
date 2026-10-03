// Interface RmlUi na GPU: executa, na ordem, os comandos gravados por RmlRender (client/ui) por cima da
// saída. É o renderizador GL3 de referência do RmlUi traduzido para SDL_GPU — camadas em texturas do
// tamanho da saída, máscara de recorte no stencil compartilhado, filtros (opacidade, desfoque em
// passes reduzidos, sombra projetada, matrizes de cor, máscara de imagem), degradês e camadas salvas
// como textura (as sombras de caixa) —, sem MSAA e sem inverter o eixo y (as texturas do SDL_GPU já
// começam em cima).
#include <algorithm>
#include <cmath>
#include <cstring>

#include "client/render/RendererImpl.h"
#include "core/Log.h"

namespace rpg::client {

namespace {

struct XfBlock {
  float transform[16];
  float translate[4];
};

struct GradientBlock {
  std::int32_t info[4];
  float pv[4];
  float positions[16];
  float colors[16][4];
};
static_assert(sizeof(GradientBlock) == 16 + 16 + 64 + 256);

struct BlurBlock {
  float texel[4];
  float region[4];
  float weights[4];
};

struct ShadowBlock {
  float color[4];
  float region[4];
};

// rect em pixels (x0, y0, x1, y1)
struct Rect {
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  int w() const { return x1 - x0; }
  int h() const { return y1 - y0; }
  bool valid() const { return x1 > x0 && y1 > y0; }
};

void sigmaToParameters(float desired, int& passLevel, float& sigma) {
  constexpr int kMaxPasses = 10;
  constexpr float kMaxSinglePassSigma = 3.0f;
  const int v = static_cast<int>(desired * (2.0f / kMaxSinglePassSigma));
  int lg = 0;
  for (int x = v; x > 1; x >>= 1) ++lg;
  passLevel = std::clamp(lg, 0, kMaxPasses);
  sigma = std::clamp(desired / static_cast<float>(1 << passLevel), 0.0f, kMaxSinglePassSigma);
}

void blurWeights(float sigma, float* w) {
  float norm = 0;
  for (int i = 0; i < 4; ++i) {
    w[i] = std::abs(sigma) < 0.1f ? (i == 0 ? 1.0f : 0.0f) : std::exp(-static_cast<float>(i * i) / (2.0f * sigma * sigma)) / (std::sqrt(2.0f * 3.14159265f) * sigma);
    norm += (i == 0 ? 1.0f : 2.0f) * w[i];
  }
  for (int i = 0; i < 4; ++i) w[i] /= norm;
}

}  // namespace

void Renderer::Impl::initRml() {
  RmlGpu& R = rml;
  if (R.ready) return;
  for (SDL_GPUTextureFormat f : {SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT})
    if (SDL_GPUTextureSupportsFormat(dev, f, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
      R.dsFmt = f;
      break;
    }
  if (R.dsFmt == SDL_GPU_TEXTUREFORMAT_INVALID) gpuFail("sem formato de profundidade com stencil para a interface");
  R.vs = shader("rml.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
  R.fsColor = shader("rml_color.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
  R.fsTex = shader("rml_texture.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
  R.fsGrad = shader("rml_gradient.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
  R.vsFull = shader("rml_fs.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
  R.fsCopy = shader("rml_copy.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
  R.fsBlur = shader("rml_blur.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
  R.fsShadow = shader("rml_shadow.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
  R.fsMatrix = shader("rml_matrix.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
  R.fsMask = shader("rml_mask.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 0);

  enum class UiBlend { Premultiplied, Replace, Constant, None };
  enum class UiStencil { Off, Equal, Replace, Incr };
  const auto make = [&](SDL_GPUShader* vs, SDL_GPUShader* fs, bool vertices, UiBlend blend, UiStencil st) {
    SDL_GPUColorTargetDescription ct{};
    ct.format = kOutFmt;
    if (blend == UiBlend::None) {
      ct.blend_state.enable_color_write_mask = true;
      ct.blend_state.color_write_mask = 0;
    } else if (blend != UiBlend::Replace) {
      ct.blend_state.enable_blend = true;
      const bool k = blend == UiBlend::Constant;
      ct.blend_state.src_color_blendfactor = k ? SDL_GPU_BLENDFACTOR_CONSTANT_COLOR : SDL_GPU_BLENDFACTOR_ONE;
      ct.blend_state.dst_color_blendfactor = k ? SDL_GPU_BLENDFACTOR_ZERO : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
      ct.blend_state.src_alpha_blendfactor = k ? SDL_GPU_BLENDFACTOR_CONSTANT_COLOR : SDL_GPU_BLENDFACTOR_ONE;
      ct.blend_state.dst_alpha_blendfactor = k ? SDL_GPU_BLENDFACTOR_ZERO : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
      ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
      ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    }
    const SDL_GPUVertexBufferDescription vb{0, sizeof(RmlVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
    const SDL_GPUVertexAttribute attrs[3] = {{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0},
                                             {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 8},
                                             {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 12}};
    SDL_GPUGraphicsPipelineCreateInfo pi{};
    pi.vertex_shader = vs;
    pi.fragment_shader = fs;
    if (vertices) pi.vertex_input_state = {&vb, 1, attrs, 3};
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pi.rasterizer_state.enable_depth_clip = false;
    SDL_GPUStencilOpState so{};
    so.fail_op = so.depth_fail_op = so.pass_op = SDL_GPU_STENCILOP_KEEP;
    so.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
    if (st == UiStencil::Equal) so.compare_op = SDL_GPU_COMPAREOP_EQUAL;
    if (st == UiStencil::Replace) so.pass_op = SDL_GPU_STENCILOP_REPLACE;
    if (st == UiStencil::Incr) so.pass_op = SDL_GPU_STENCILOP_INCREMENT_AND_CLAMP;
    pi.depth_stencil_state.enable_stencil_test = st != UiStencil::Off;
    pi.depth_stencil_state.front_stencil_state = so;
    pi.depth_stencil_state.back_stencil_state = so;
    pi.depth_stencil_state.compare_mask = 0xFF;
    pi.depth_stencil_state.write_mask = (st == UiStencil::Replace || st == UiStencil::Incr) ? 0xFF : 0x00;
    pi.target_info.color_target_descriptions = &ct;
    pi.target_info.num_color_targets = 1;
    pi.target_info.has_depth_stencil_target = true;
    pi.target_info.depth_stencil_format = R.dsFmt;
    SDL_GPUGraphicsPipeline* p = SDL_CreateGPUGraphicsPipeline(dev, &pi);
    if (!p) gpuFail("SDL_CreateGPUGraphicsPipeline (interface)");
    return p;
  };
  for (int c = 0; c < 2; ++c) {
    const UiStencil st = c ? UiStencil::Equal : UiStencil::Off;
    R.color[c] = make(R.vs, R.fsColor, true, UiBlend::Premultiplied, st);
    R.tex[c] = make(R.vs, R.fsTex, true, UiBlend::Premultiplied, st);
    R.grad[c] = make(R.vs, R.fsGrad, true, UiBlend::Premultiplied, st);
  }
  R.clipReplace = make(R.vs, R.fsColor, true, UiBlend::None, UiStencil::Replace);
  R.clipIncr = make(R.vs, R.fsColor, true, UiBlend::None, UiStencil::Incr);
  R.stencilClear = make(R.vsFull, R.fsCopy, false, UiBlend::None, UiStencil::Replace);
  R.copyBlend = make(R.vsFull, R.fsCopy, false, UiBlend::Premultiplied, UiStencil::Off);
  R.copyReplace = make(R.vsFull, R.fsCopy, false, UiBlend::Replace, UiStencil::Off);
  R.copyOpacity = make(R.vsFull, R.fsCopy, false, UiBlend::Constant, UiStencil::Off);
  R.blur = make(R.vsFull, R.fsBlur, false, UiBlend::Replace, UiStencil::Off);
  R.shadow = make(R.vsFull, R.fsShadow, false, UiBlend::Replace, UiStencil::Off);
  R.matrix = make(R.vsFull, R.fsMatrix, false, UiBlend::Replace, UiStencil::Off);
  R.mask = make(R.vsFull, R.fsMask, false, UiBlend::Replace, UiStencil::Off);

  SDL_GPUSamplerCreateInfo si{};
  si.min_filter = si.mag_filter = SDL_GPU_FILTER_LINEAR;
  si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  R.linear = SDL_CreateGPUSampler(dev, &si);
  const std::uint8_t clear[4] = {0, 0, 0, 0};
  R.clearTex = uploadTexture(SDL_GPU_TEXTURETYPE_2D, kOutFmt, 1, 1, 1, 1, {{clear}}, 4, false);
  si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  R.clampLinear = SDL_CreateGPUSampler(dev, &si);
  if (!R.linear || !R.clampLinear) gpuFail("SDL_CreateGPUSampler (interface)");
  R.ready = true;
}

void Renderer::Impl::releaseRml() {
  RmlGpu& R = rml;
  if (!dev) return;
  for (auto& [h, t] : R.textures)
    if (t.tex) SDL_ReleaseGPUTexture(dev, t.tex);
  for (std::size_t i = 1; i < R.layers.size(); ++i)
    if (R.layers[i]) SDL_ReleaseGPUTexture(dev, R.layers[i]);
  for (SDL_GPUTexture* t : R.post)
    if (t) SDL_ReleaseGPUTexture(dev, t);
  for (SDL_GPUTexture* t : {R.maskTex, R.ds, R.clearTex})
    if (t) SDL_ReleaseGPUTexture(dev, t);
  for (SDL_GPUBuffer* b : {R.vb.buf, R.ib.buf})
    if (b) SDL_ReleaseGPUBuffer(dev, b);
  for (SDL_GPUGraphicsPipeline* p : {R.color[0], R.color[1], R.tex[0], R.tex[1], R.grad[0], R.grad[1], R.clipReplace, R.clipIncr, R.stencilClear,
                                     R.copyBlend, R.copyReplace, R.copyOpacity, R.blur, R.shadow, R.matrix, R.mask})
    if (p) SDL_ReleaseGPUGraphicsPipeline(dev, p);
  for (SDL_GPUShader* s : {R.vs, R.fsColor, R.fsTex, R.fsGrad, R.vsFull, R.fsCopy, R.fsBlur, R.fsShadow, R.fsMatrix, R.fsMask})
    if (s) SDL_ReleaseGPUShader(dev, s);
  for (SDL_GPUSampler* s : {R.linear, R.clampLinear})
    if (s) SDL_ReleaseGPUSampler(dev, s);
  R = RmlGpu{};
}

void Renderer::Impl::ensureRmlTargets(int w, int h) {
  RmlGpu& R = rml;
  if (R.w == w && R.h == h && R.ds) return;
  for (std::size_t i = 1; i < R.layers.size(); ++i)
    if (R.layers[i]) SDL_ReleaseGPUTexture(dev, R.layers[i]);
  for (SDL_GPUTexture*& t : R.post)
    if (t) SDL_ReleaseGPUTexture(dev, t);
  for (SDL_GPUTexture* t : {R.maskTex, R.ds})
    if (t) SDL_ReleaseGPUTexture(dev, t);
  R.layers.assign(1, nullptr);
  const SDL_GPUTextureUsageFlags rt = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
  for (SDL_GPUTexture*& t : R.post) t = texture(kOutFmt, rt, w, h);
  R.maskTex = texture(kOutFmt, rt, w, h);
  R.ds = texture(R.dsFmt, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, w, h);
  R.w = w;
  R.h = h;
}

// Texturas novas, alteradas (dinâmicas) e soltas. As novas sobem já (comando próprio, antes do quadro).
void Renderer::Impl::syncRmlTextures(const RmlRender& r) {
  RmlGpu& R = rml;
  for (std::uint64_t h : r.releasedTextures()) {
    const auto it = R.textures.find(h);
    if (it == R.textures.end()) continue;
    if (it->second.tex) SDL_ReleaseGPUTexture(dev, it->second.tex);
    R.textures.erase(it);
  }
  const auto ensure = [&](std::uint64_t h) {
    if (!h) return;
    const RmlTexture* t = r.texture(h);
    if (!t || t->renderTarget || t->rgba.empty()) return;
    RmlGpu::Tex& g = R.textures[h];
    if (g.tex && g.version == t->version) return;
    if (g.tex) SDL_ReleaseGPUTexture(dev, g.tex);
    g.tex = uploadTexture(SDL_GPU_TEXTURETYPE_2D, kOutFmt, t->w, t->h, 1, 1, {{t->rgba.data()}}, 4, false);
    g.version = t->version;
  };
  for (const RmlCmd& c : r.commands())
    if (c.op == RmlOp::Draw) ensure(c.texture);
}

void Renderer::Impl::packRml(const RmlRender& r) {
  RmlGpu& R = rml;
  R.slices.clear();
  R.vtx.clear();
  R.idx.clear();
  std::size_t vcount = 0, icount = 0;
  for (const RmlCmd& c : r.commands()) {
    if (c.op != RmlOp::Draw && c.op != RmlOp::Shader && c.op != RmlOp::ClipMask) continue;
    if (R.slices.contains(c.geometry)) continue;
    const RmlGeometry* g = r.geometry(c.geometry);
    if (!g || g->indices.empty()) continue;
    RmlGpu::Slice s;
    s.baseVertex = static_cast<Sint32>(vcount);
    s.firstIndex = static_cast<Uint32>(icount);
    s.indexCount = static_cast<Uint32>(g->indices.size());
    R.vtx.insert(R.vtx.end(), reinterpret_cast<const std::uint8_t*>(g->vertices.data()),
                 reinterpret_cast<const std::uint8_t*>(g->vertices.data() + g->vertices.size()));
    R.idx.insert(R.idx.end(), reinterpret_cast<const std::uint8_t*>(g->indices.data()),
                 reinterpret_cast<const std::uint8_t*>(g->indices.data() + g->indices.size()));
    vcount += g->vertices.size();
    icount += g->indices.size();
    R.slices.emplace(c.geometry, s);
  }
}

void Renderer::Impl::drawRml(SDL_GPUCommandBuffer* cmd, const RmlRender& r, FrameStats& st) {
  RmlGpu& R = rml;
  const int W = texW, H = texH;
  ensureRmlTargets(W, H);
  R.layers[0] = out;

  // projeção: pixels (y para baixo) → NDC do SDL_GPU (y para cima); z entre −10000 e 10000 vai para 0–1
  glm::mat4 proj(1.0f);
  proj[0][0] = 2.0f / static_cast<float>(W);
  proj[1][1] = -2.0f / static_cast<float>(H);
  proj[2][2] = 1.0f / 20000.0f;
  proj[3] = glm::vec4(-1.0f, 1.0f, 0.5f, 1.0f);
  glm::mat4 xf = proj;

  std::vector<int> stack{0};
  SDL_GPURenderPass* rp = nullptr;
  int passTarget = -1;
  bool clearNext = false, stencilCleared = false;
  Rect scissor{0, 0, W, H};
  bool scissorOn = false;
  bool clipOn = false;
  Uint8 stencilRef = 1;

  const auto layerTex = [&](int i) -> SDL_GPUTexture* {
    const auto n = static_cast<std::size_t>(i);
    if (R.layers.size() <= n) R.layers.resize(n + 1, nullptr);
    if (!R.layers[n]) R.layers[n] = texture(kOutFmt, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, W, H);
    return R.layers[n];
  };
  const auto end = [&] {
    if (rp) SDL_EndGPURenderPass(rp);
    rp = nullptr;
    passTarget = -1;
  };
  const auto region = [&]() -> Rect {
    if (!scissorOn) return Rect{0, 0, W, H};
    Rect s{std::clamp(scissor.x0, 0, W), std::clamp(scissor.y0, 0, H), std::clamp(scissor.x1, 0, W), std::clamp(scissor.y1, 0, H)};
    return s;
  };
  const auto applyScissor = [&](SDL_GPURenderPass* pass, const Rect& s) {
    SDL_Rect sr{s.x0, s.y0, std::max(0, s.w()), std::max(0, s.h())};
    SDL_SetGPUScissor(pass, &sr);
  };
  // passe sobre uma textura qualquer (camada ou pós-processo), com o stencil compartilhado
  const auto beginOn = [&](SDL_GPUTexture* tex, bool clear) {
    SDL_GPUColorTargetInfo ct{};
    ct.texture = tex;
    ct.load_op = clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    ct.clear_color = {0, 0, 0, 0};
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo ds{};
    ds.texture = R.ds;
    ds.load_op = SDL_GPU_LOADOP_DONT_CARE;
    ds.store_op = SDL_GPU_STOREOP_DONT_CARE;
    ds.stencil_load_op = stencilCleared ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
    ds.stencil_store_op = SDL_GPU_STOREOP_STORE;
    ds.clear_stencil = 0;
    stencilCleared = true;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, &ds);
    SDL_GPUViewport vp{0, 0, static_cast<float>(W), static_cast<float>(H), 0, 1};
    SDL_SetGPUViewport(pass, &vp);
    SDL_SetGPUStencilReference(pass, stencilRef);
    return pass;
  };
  const auto ensurePass = [&] {
    const int top = stack.back();
    if (rp && passTarget == top) return;
    end();
    rp = beginOn(layerTex(top), clearNext);
    clearNext = false;
    passTarget = top;
    applyScissor(rp, region());
  };
  const auto vertexXf = [&](glm::vec2 translation) {
    XfBlock b{};
    std::memcpy(b.transform, &xf[0][0], sizeof b.transform);
    b.translate[0] = translation.x;
    b.translate[1] = translation.y;
    SDL_PushGPUVertexUniformData(cmd, 0, &b, sizeof b);
  };
  const auto bindGeometry = [&](std::uint64_t g) -> const RmlGpu::Slice* {
    const auto it = R.slices.find(g);
    if (it == R.slices.end()) return nullptr;
    const SDL_GPUBufferBinding vb{R.vb.buf, 0};
    SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
    const SDL_GPUBufferBinding ib{R.ib.buf, 0};
    SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    return &it->second;
  };
  const auto drawSlice = [&](const RmlGpu::Slice& s) {
    SDL_DrawGPUIndexedPrimitives(rp, s.indexCount, 1, s.firstIndex, s.baseVertex, 0);
    ++st.drawCalls;
  };
  // passe de tela cheia: lê `src` e escreve em `dst` (pós-processo), com o recorte da janela
  const auto fullscreen = [&](SDL_GPUTexture* dst, bool clear, SDL_GPUGraphicsPipeline* p, SDL_GPUTexture* src, glm::vec4 uv, const Rect& sc,
                              const SDL_GPUViewport* viewport = nullptr) {
    SDL_GPURenderPass* pass = beginOn(dst, clear);
    if (viewport) SDL_SetGPUViewport(pass, viewport);
    applyScissor(pass, sc);
    SDL_BindGPUGraphicsPipeline(pass, p);
    SDL_PushGPUVertexUniformData(cmd, 0, &uv, sizeof uv);
    const SDL_GPUTextureSamplerBinding b{src, R.clampLinear};
    SDL_BindGPUFragmentSamplers(pass, 0, &b, 1);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    ++st.drawCalls;
    return pass;
  };
  const auto copyAll = [&](SDL_GPUTexture* src, SDL_GPUTexture* dst) {
    SDL_GPUBlitInfo bi{};
    bi.source.texture = src;
    bi.source.w = static_cast<Uint32>(W);
    bi.source.h = static_cast<Uint32>(H);
    bi.destination.texture = dst;
    bi.destination.w = static_cast<Uint32>(W);
    bi.destination.h = static_cast<Uint32>(H);
    bi.load_op = SDL_GPU_LOADOP_DONT_CARE;
    bi.filter = SDL_GPU_FILTER_NEAREST;
    SDL_BlitGPUTexture(cmd, &bi);
  };
  // RenderBlur do GL3: reduções sucessivas pela metade, desfoque vertical e horizontal na resolução
  // reduzida e ampliação de volta para a janela. `a` tem a imagem e recebe o resultado; `b` é rascunho.
  const auto blurInto = [&](float sigmaIn, SDL_GPUTexture* a, SDL_GPUTexture* b, const Rect& win) {
    int level = 0;
    float sigma = 0;
    sigmaToParameters(sigmaIn, level, sigma);
    Rect sc = win;
    const glm::vec2 uvScale((W % 2) ? 1.0f - 1.0f / static_cast<float>(W) : 1.0f, (H % 2) ? 1.0f - 1.0f / static_cast<float>(H) : 1.0f);
    const SDL_GPUViewport half{0, 0, static_cast<float>(W / 2), static_cast<float>(H / 2), 0, 1};
    for (int i = 0; i < level; ++i) {
      sc.x0 = (sc.x0 + 1) / 2;
      sc.y0 = (sc.y0 + 1) / 2;
      sc.x1 = std::max(sc.x1 / 2, sc.x0);
      sc.y1 = std::max(sc.y1 / 2, sc.y0);
      const bool fromA = i % 2 == 0;
      SDL_EndGPURenderPass(fullscreen(fromA ? b : a, false, R.copyReplace, fromA ? a : b, glm::vec4(uvScale, 0, 0), sc, &half));
    }
    if (level % 2 == 0) SDL_EndGPURenderPass(fullscreen(b, false, R.copyReplace, a, glm::vec4(1, 1, 0, 0), Rect{0, 0, W, H}));
    BlurBlock bb{};
    blurWeights(sigma, bb.weights);
    bb.region[0] = (static_cast<float>(sc.x0) + 0.5f) / static_cast<float>(W);
    bb.region[1] = (static_cast<float>(sc.y0) + 0.5f) / static_cast<float>(H);
    bb.region[2] = (static_cast<float>(sc.x1) - 0.5f) / static_cast<float>(W);
    bb.region[3] = (static_cast<float>(sc.y1) - 0.5f) / static_cast<float>(H);
    // vertical: b → a
    bb.texel[0] = 0;
    bb.texel[1] = 1.0f / static_cast<float>(H);
    SDL_GPURenderPass* p = beginOn(a, false);
    applyScissor(p, sc);
    SDL_BindGPUGraphicsPipeline(p, R.blur);
    const glm::vec4 uv1(1, 1, 0, 0);
    SDL_PushGPUVertexUniformData(cmd, 0, &uv1, sizeof uv1);
    SDL_PushGPUFragmentUniformData(cmd, 0, &bb, sizeof bb);
    SDL_GPUTextureSamplerBinding sb{b, R.clampLinear};
    SDL_BindGPUFragmentSamplers(p, 0, &sb, 1);
    SDL_DrawGPUPrimitives(p, 3, 1, 0, 0);
    SDL_EndGPURenderPass(p);
    // horizontal: a → b, com uma borda transparente de 1 px em volta (evita sangrar na ampliação)
    const Rect ext{std::max(sc.x0 - 1, 0), std::max(sc.y0 - 1, 0), std::min(sc.x1 + 1, W), std::min(sc.y1 + 1, H)};
    p = beginOn(b, false);
    applyScissor(p, ext);
    SDL_BindGPUGraphicsPipeline(p, R.copyReplace);
    SDL_PushGPUVertexUniformData(cmd, 0, &uv1, sizeof uv1);
    const SDL_GPUTextureSamplerBinding cb{R.clearTex, R.clampLinear};
    SDL_BindGPUFragmentSamplers(p, 0, &cb, 1);
    SDL_DrawGPUPrimitives(p, 3, 1, 0, 0);
    applyScissor(p, sc);
    bb.texel[0] = 1.0f / static_cast<float>(W);
    bb.texel[1] = 0;
    SDL_BindGPUGraphicsPipeline(p, R.blur);
    SDL_PushGPUVertexUniformData(cmd, 0, &uv1, sizeof uv1);
    SDL_PushGPUFragmentUniformData(cmd, 0, &bb, sizeof bb);
    sb = {a, R.clampLinear};
    SDL_BindGPUFragmentSamplers(p, 0, &sb, 1);
    SDL_DrawGPUPrimitives(p, 3, 1, 0, 0);
    SDL_EndGPURenderPass(p);
    st.drawCalls += 2;
    // ampliação: a região reduzida de b volta para a janela de a (e, alinhada à potência de 2, por cima)
    const auto upscale = [&](const Rect& target) {
      if (!target.valid() || !sc.valid()) return;
      const SDL_GPUViewport vp{static_cast<float>(target.x0), static_cast<float>(target.y0), static_cast<float>(target.w()),
                               static_cast<float>(target.h()), 0, 1};
      const glm::vec4 uv(static_cast<float>(sc.w()) / static_cast<float>(W), static_cast<float>(sc.h()) / static_cast<float>(H),
                         static_cast<float>(sc.x0) / static_cast<float>(W), static_cast<float>(sc.y0) / static_cast<float>(H));
      SDL_EndGPURenderPass(fullscreen(a, false, R.copyReplace, b, uv, win, &vp));
    };
    upscale(win);
    const Rect pow2{sc.x0 << level, sc.y0 << level, sc.x1 << level, sc.y1 << level};
    if (pow2.x0 != win.x0 || pow2.y0 != win.y0 || pow2.x1 != win.x1 || pow2.y1 != win.y1) upscale(pow2);
  };
  const auto runFilters = [&](int first, int count) {
    const Rect win = region();
    for (int i = 0; i < count; ++i) {
      const RmlFilter* f = r.filter(r.filterRefs()[static_cast<std::size_t>(first + i)]);
      if (!f) continue;
      switch (f->type) {
        case RmlFilter::Type::Opacity: {
          SDL_GPURenderPass* p = beginOn(R.post[1], true);
          applyScissor(p, win);
          SDL_BindGPUGraphicsPipeline(p, R.copyOpacity);
          const SDL_FColor k{f->value, f->value, f->value, f->value};
          SDL_SetGPUBlendConstants(p, k);
          const glm::vec4 uv(1, 1, 0, 0);
          SDL_PushGPUVertexUniformData(cmd, 0, &uv, sizeof uv);
          const SDL_GPUTextureSamplerBinding b{R.post[0], R.clampLinear};
          SDL_BindGPUFragmentSamplers(p, 0, &b, 1);
          SDL_DrawGPUPrimitives(p, 3, 1, 0, 0);
          SDL_EndGPURenderPass(p);
          std::swap(R.post[0], R.post[1]);
          break;
        }
        case RmlFilter::Type::Blur: blurInto(f->sigma, R.post[0], R.post[1], win); break;
        case RmlFilter::Type::DropShadow: {
          ShadowBlock sh{};
          std::memcpy(sh.color, &f->color[0], sizeof sh.color);
          sh.region[0] = (static_cast<float>(win.x0) + 0.5f) / static_cast<float>(W);
          sh.region[1] = (static_cast<float>(win.y0) + 0.5f) / static_cast<float>(H);
          sh.region[2] = (static_cast<float>(win.x1) - 0.5f) / static_cast<float>(W);
          sh.region[3] = (static_cast<float>(win.y1) - 0.5f) / static_cast<float>(H);
          SDL_GPURenderPass* p = beginOn(R.post[1], false);
          applyScissor(p, win);
          SDL_BindGPUGraphicsPipeline(p, R.shadow);
          const glm::vec4 uv(1, 1, -f->offset.x / static_cast<float>(W), -f->offset.y / static_cast<float>(H));
          SDL_PushGPUVertexUniformData(cmd, 0, &uv, sizeof uv);
          SDL_PushGPUFragmentUniformData(cmd, 0, &sh, sizeof sh);
          const SDL_GPUTextureSamplerBinding b{R.post[0], R.clampLinear};
          SDL_BindGPUFragmentSamplers(p, 0, &b, 1);
          SDL_DrawGPUPrimitives(p, 3, 1, 0, 0);
          SDL_EndGPURenderPass(p);
          if (f->sigma >= 0.5f) blurInto(f->sigma, R.post[1], R.post[2], win);
          SDL_EndGPURenderPass(fullscreen(R.post[1], false, R.copyBlend, R.post[0], glm::vec4(1, 1, 0, 0), win));
          std::swap(R.post[0], R.post[1]);
          break;
        }
        case RmlFilter::Type::ColorMatrix: {
          SDL_GPURenderPass* p = beginOn(R.post[1], false);
          applyScissor(p, win);
          SDL_BindGPUGraphicsPipeline(p, R.matrix);
          const glm::vec4 uv(1, 1, 0, 0);
          SDL_PushGPUVertexUniformData(cmd, 0, &uv, sizeof uv);
          SDL_PushGPUFragmentUniformData(cmd, 0, &f->colorMatrix[0][0], sizeof(glm::mat4));
          const SDL_GPUTextureSamplerBinding b{R.post[0], R.clampLinear};
          SDL_BindGPUFragmentSamplers(p, 0, &b, 1);
          SDL_DrawGPUPrimitives(p, 3, 1, 0, 0);
          SDL_EndGPURenderPass(p);
          std::swap(R.post[0], R.post[1]);
          break;
        }
        case RmlFilter::Type::MaskImage: {
          SDL_GPURenderPass* p = beginOn(R.post[1], false);
          applyScissor(p, win);
          SDL_BindGPUGraphicsPipeline(p, R.mask);
          const glm::vec4 uv(1, 1, 0, 0);
          SDL_PushGPUVertexUniformData(cmd, 0, &uv, sizeof uv);
          const SDL_GPUTextureSamplerBinding b[2] = {{R.post[0], R.clampLinear}, {R.maskTex, R.clampLinear}};
          SDL_BindGPUFragmentSamplers(p, 0, b, 2);
          SDL_DrawGPUPrimitives(p, 3, 1, 0, 0);
          SDL_EndGPURenderPass(p);
          std::swap(R.post[0], R.post[1]);
          break;
        }
      }
    }
  };

  for (const RmlCmd& c : r.commands()) {
    switch (c.op) {
      case RmlOp::Draw:
      case RmlOp::Shader: {
        ensurePass();
        const RmlGpu::Slice* s = bindGeometry(c.geometry);
        if (!s) break;
        const int clip = clipOn ? 1 : 0;
        if (c.op == RmlOp::Shader) {
          const RmlGradient* g = r.gradient(c.shader);
          if (!g) break;
          GradientBlock gb{};
          gb.info[0] = g->func;
          gb.info[1] = g->stops;
          gb.pv[0] = g->p.x;
          gb.pv[1] = g->p.y;
          gb.pv[2] = g->v.x;
          gb.pv[3] = g->v.y;
          for (int i = 0; i < g->stops; ++i) {
            gb.positions[i] = g->positions[static_cast<std::size_t>(i)];
            std::memcpy(gb.colors[i], &g->colors[static_cast<std::size_t>(i)][0], sizeof(float) * 4);
          }
          SDL_BindGPUGraphicsPipeline(rp, R.grad[clip]);
          SDL_PushGPUFragmentUniformData(cmd, 0, &gb, sizeof gb);
        } else if (c.texture) {
          const auto it = R.textures.find(c.texture);
          if (it == R.textures.end() || !it->second.tex) break;
          SDL_BindGPUGraphicsPipeline(rp, R.tex[clip]);
          const SDL_GPUTextureSamplerBinding b{it->second.tex, R.linear};
          SDL_BindGPUFragmentSamplers(rp, 0, &b, 1);
        } else {
          SDL_BindGPUGraphicsPipeline(rp, R.color[clip]);
        }
        vertexXf(c.translation);
        drawSlice(*s);
        st.triangles += s->indexCount / 3;
        break;
      }
      case RmlOp::Scissor:
        scissorOn = c.enabled;
        scissor = Rect{c.rect[0], c.rect[1], c.rect[2], c.rect[3]};
        if (rp) applyScissor(rp, region());
        break;
      case RmlOp::Transform:
        xf = c.index >= 0 ? proj * r.transforms()[static_cast<std::size_t>(c.index)] : proj;
        break;
      case RmlOp::ClipEnable:
        clipOn = c.enabled;
        break;
      case RmlOp::ClipMask: {
        ensurePass();
        if (c.clipOp != 2) {
          // Set: zera (SetInverse: põe 1) dentro do recorte e escreve o outro valor na geometria
          SDL_SetGPUStencilReference(rp, c.clipOp == 0 ? 0 : 1);
          SDL_BindGPUGraphicsPipeline(rp, R.stencilClear);
          const glm::vec4 uv(1, 1, 0, 0);
          SDL_PushGPUVertexUniformData(cmd, 0, &uv, sizeof uv);
          const SDL_GPUTextureSamplerBinding b{R.maskTex, R.clampLinear};
          SDL_BindGPUFragmentSamplers(rp, 0, &b, 1);
          SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
          SDL_SetGPUStencilReference(rp, c.clipOp == 0 ? 1 : 0);
          SDL_BindGPUGraphicsPipeline(rp, R.clipReplace);
          stencilRef = 1;
        } else {
          SDL_BindGPUGraphicsPipeline(rp, R.clipIncr);
          stencilRef = static_cast<Uint8>(stencilRef + 1);
        }
        if (const RmlGpu::Slice* s = bindGeometry(c.geometry)) {
          vertexXf(c.translation);
          drawSlice(*s);
        }
        SDL_SetGPUStencilReference(rp, stencilRef);
        break;
      }
      case RmlOp::PushLayer:
        end();
        stack.push_back(c.layer);
        layerTex(c.layer);
        clearNext = true;
        break;
      case RmlOp::PopLayer:
        end();
        if (stack.size() > 1) stack.pop_back();
        break;
      case RmlOp::Composite: {
        end();
        copyAll(layerTex(c.layer), R.post[0]);
        runFilters(c.filterFirst, c.filterCount);
        SDL_EndGPURenderPass(fullscreen(layerTex(c.dstLayer), false, c.blend ? R.copyReplace : R.copyBlend, R.post[0], glm::vec4(1, 1, 0, 0),
                                        region()));
        break;
      }
      case RmlOp::SaveTexture: {
        end();
        const RmlTexture* t = r.texture(c.texture);
        if (!t) break;
        RmlGpu::Tex& g = R.textures[c.texture];
        if (g.tex) SDL_ReleaseGPUTexture(dev, g.tex);
        g.tex = texture(kOutFmt, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, t->w, t->h);
        g.version = t->version;
        SDL_GPUBlitInfo bi{};
        bi.source.texture = layerTex(stack.back());
        bi.source.x = static_cast<Uint32>(std::max(0, c.rect[0]));
        bi.source.y = static_cast<Uint32>(std::max(0, c.rect[1]));
        bi.source.w = static_cast<Uint32>(std::min(t->w, W - std::max(0, c.rect[0])));
        bi.source.h = static_cast<Uint32>(std::min(t->h, H - std::max(0, c.rect[1])));
        if (bi.source.w == 0 || bi.source.h == 0) break;
        bi.destination.texture = g.tex;
        bi.destination.w = bi.source.w;
        bi.destination.h = bi.source.h;
        bi.load_op = SDL_GPU_LOADOP_DONT_CARE;
        bi.filter = SDL_GPU_FILTER_NEAREST;
        SDL_BlitGPUTexture(cmd, &bi);
        break;
      }
      case RmlOp::SaveMask:
        end();
        copyAll(layerTex(stack.back()), R.maskTex);
        break;
    }
  }
  end();
}

}  // namespace rpg::client
