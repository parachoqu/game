#include "client/render/Renderer.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

#include <SDL3/SDL.h>

#include "client/geom/Primitives.h"
#include "client/image/PngWriter.h"
#include "client/render/ShaderBlobs.h"

namespace rpg::client {

namespace {

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error(what + ": " + SDL_GetError()); }

struct GpuMesh {
  SDL_GPUBuffer* vb = nullptr;
  SDL_GPUBuffer* ib = nullptr;
  Uint32 indexCount = 0;
};

struct ChunkDraw {
  Uint32 firstIndex[2] = {0, 0}, indexCount[2] = {0, 0};
  Sint32 vertexOffset[2] = {0, 0};
  glm::vec3 center{0.0f};
  float radius = 0;
};

struct GpuMap {
  bool loaded = false;
  SDL_GPUBuffer* terrainVB = nullptr;
  SDL_GPUBuffer* terrainIB = nullptr;
  std::vector<ChunkDraw> chunks;
  GpuMesh water, bridges;
  SDL_GPUBuffer* props = nullptr;
  std::array<Uint32, kPrimCount> propFirst{}, propCount{};
};

struct DynBuffer {
  SDL_GPUBuffer* buf = nullptr;
  Uint32 cap = 0;
};

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

}  // namespace

struct Renderer::Impl {
  SDL_Window* window = nullptr;
  SDL_GPUDevice* dev = nullptr;
  SDL_GPUTextureFormat swapFmt = SDL_GPU_TEXTUREFORMAT_INVALID;
  SDL_GPUTextureFormat depthFmt = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
  static constexpr SDL_GPUTextureFormat kHdrFmt = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
  static constexpr SDL_GPUTextureFormat kOutFmt = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

  SDL_GPUGraphicsPipeline *scene = nullptr, *water = nullptr, *unlit = nullptr, *sky = nullptr, *composite = nullptr, *ui = nullptr;
  SDL_GPUSampler* linear = nullptr;
  SDL_GPUTexture *hdr = nullptr, *depth = nullptr, *out = nullptr, *atlas = nullptr;
  int texW = 0, texH = 0, atlasSize = 0;
  std::uint32_t atlasGeneration = ~0u;

  std::array<GpuMesh, kPrimCount> prims;
  SDL_GPUBuffer* identity = nullptr;
  std::array<GpuMap, 2> maps;
  DynBuffer instances, unlitVerts, uiVerts;
  SDL_GPUTransferBuffer* staging = nullptr;
  Uint32 stagingCap = 0;
  SDL_GPUTransferBuffer* download = nullptr;
  Uint32 downloadCap = 0;

  SDL_GPUShader* shader(const char* name, SDL_GPUShaderStage stage, Uint32 samplers, Uint32 uniforms) {
    const auto code = shaderBlob(name);
    if (code.empty()) throw std::runtime_error(std::string("shader não embutido: ") + name);
    SDL_GPUShaderCreateInfo ci{};
    ci.code = code.data();
    ci.code_size = code.size();
    ci.entrypoint = "main";
    ci.format = SDL_GPU_SHADERFORMAT_SPIRV;
    ci.stage = stage;
    ci.num_samplers = samplers;
    ci.num_uniform_buffers = uniforms;
    SDL_GPUShader* s = SDL_CreateGPUShader(dev, &ci);
    if (!s) fail(std::string("SDL_CreateGPUShader(") + name + ")");
    return s;
  }

  SDL_GPUBuffer* buffer(SDL_GPUBufferUsageFlags usage, Uint32 size) {
    SDL_GPUBufferCreateInfo ci{};
    ci.usage = usage;
    ci.size = std::max<Uint32>(size, 16);
    SDL_GPUBuffer* b = SDL_CreateGPUBuffer(dev, &ci);
    if (!b) fail("SDL_CreateGPUBuffer");
    return b;
  }

  // Sobe dados estáticos na hora (comando próprio).
  SDL_GPUBuffer* staticBuffer(SDL_GPUBufferUsageFlags usage, const void* data, Uint32 size) {
    SDL_GPUBuffer* b = buffer(usage, size);
    if (size == 0) return b;
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = size;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &ti);
    if (!tb) fail("SDL_CreateGPUTransferBuffer");
    void* dst = SDL_MapGPUTransferBuffer(dev, tb, false);
    std::memcpy(dst, data, size);
    SDL_UnmapGPUTransferBuffer(dev, tb);
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src{tb, 0};
    SDL_GPUBufferRegion reg{b, 0, size};
    SDL_UploadToGPUBuffer(cp, &src, &reg, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(dev, tb);
    return b;
  }

  GpuMesh mesh(const MeshData& m) {
    GpuMesh g;
    g.vb = staticBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, m.vertices.data(), static_cast<Uint32>(m.vertices.size() * sizeof(Vertex)));
    g.ib = staticBuffer(SDL_GPU_BUFFERUSAGE_INDEX, m.indices.data(), static_cast<Uint32>(m.indices.size() * sizeof(std::uint32_t)));
    g.indexCount = static_cast<Uint32>(m.indices.size());
    return g;
  }

  void release(GpuMesh& m) {
    if (m.vb) SDL_ReleaseGPUBuffer(dev, m.vb);
    if (m.ib) SDL_ReleaseGPUBuffer(dev, m.ib);
    m = {};
  }

  SDL_GPUTexture* texture(SDL_GPUTextureFormat fmt, SDL_GPUTextureUsageFlags usage, int w, int h) {
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = fmt;
    ci.usage = usage;
    ci.width = static_cast<Uint32>(w);
    ci.height = static_cast<Uint32>(h);
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    SDL_GPUTexture* t = SDL_CreateGPUTexture(dev, &ci);
    if (!t) fail("SDL_CreateGPUTexture");
    return t;
  }

  void ensureTargets(int w, int h) {
    if (w == texW && h == texH && hdr) return;
    if (hdr) SDL_ReleaseGPUTexture(dev, hdr);
    if (depth) SDL_ReleaseGPUTexture(dev, depth);
    if (out) SDL_ReleaseGPUTexture(dev, out);
    hdr = texture(kHdrFmt, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, w, h);
    depth = texture(depthFmt, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, w, h);
    out = texture(kOutFmt, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, w, h);
    texW = w;
    texH = h;
  }

  void ensureDyn(DynBuffer& d, SDL_GPUBufferUsageFlags usage, Uint32 size) {
    if (size <= d.cap) return;
    if (d.buf) SDL_ReleaseGPUBuffer(dev, d.buf);
    d.cap = std::max<Uint32>(size + size / 2, 64 * 1024);
    d.buf = buffer(usage, d.cap);
  }

  SDL_GPUGraphicsPipeline* pipeline(SDL_GPUShader* vs, SDL_GPUShader* fs, const SDL_GPUVertexInputState& vin,
                                    SDL_GPUTextureFormat color, bool blend, bool depthTarget, bool depthTest, bool depthWrite,
                                    SDL_GPUPrimitiveType prim = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST) {
    SDL_GPUColorTargetDescription ct{};
    ct.format = color;
    if (blend) {
      ct.blend_state.enable_blend = true;
      ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
      ct.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
      ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
      ct.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
      ct.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
      ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    }
    SDL_GPUGraphicsPipelineCreateInfo pi{};
    pi.vertex_shader = vs;
    pi.fragment_shader = fs;
    pi.vertex_input_state = vin;
    pi.primitive_type = prim;
    pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pi.rasterizer_state.enable_depth_clip = true;
    pi.depth_stencil_state.enable_depth_test = depthTest;
    pi.depth_stencil_state.enable_depth_write = depthWrite;
    pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    pi.target_info.color_target_descriptions = &ct;
    pi.target_info.num_color_targets = 1;
    pi.target_info.has_depth_stencil_target = depthTarget;
    pi.target_info.depth_stencil_format = depthFmt;
    SDL_GPUGraphicsPipeline* p = SDL_CreateGPUGraphicsPipeline(dev, &pi);
    if (!p) fail("SDL_CreateGPUGraphicsPipeline");
    return p;
  }

  void createPipelines() {
    // cena iluminada: vértice (pos, normal, cor) + instância (matriz, cor)
    const SDL_GPUVertexBufferDescription sceneBufs[2] = {
        {0, sizeof(Vertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0},
        {1, sizeof(Instance), SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0},
    };
    const SDL_GPUVertexAttribute sceneAttrs[8] = {
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
        {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 24},  {3, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0},
        {4, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 16},       {5, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 32},
        {6, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 48},       {7, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 64},
    };
    const SDL_GPUVertexInputState sceneIn{sceneBufs, 2, sceneAttrs, 8};
    const SDL_GPUVertexBufferDescription unlitBuf{0, sizeof(ColorVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
    const SDL_GPUVertexAttribute unlitAttrs[2] = {{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
                                                  {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 12}};
    const SDL_GPUVertexInputState unlitIn{&unlitBuf, 1, unlitAttrs, 2};
    const SDL_GPUVertexBufferDescription uiBuf{0, sizeof(UiVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
    const SDL_GPUVertexAttribute uiAttrs[3] = {{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0},
                                               {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 8},
                                               {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 16}};
    const SDL_GPUVertexInputState uiIn{&uiBuf, 1, uiAttrs, 3};
    const SDL_GPUVertexInputState none{nullptr, 0, nullptr, 0};

    SDL_GPUShader* sceneV = shader("scene.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader* sceneF = shader("scene.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
    SDL_GPUShader* waterF = shader("water.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
    SDL_GPUShader* unlitV = shader("unlit.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader* unlitF = shader("unlit.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
    SDL_GPUShader* fullV = shader("fullscreen.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
    SDL_GPUShader* skyF = shader("sky.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
    SDL_GPUShader* compF = shader("composite.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    SDL_GPUShader* uiV = shader("ui.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader* uiF = shader("ui.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);

    scene = pipeline(sceneV, sceneF, sceneIn, kHdrFmt, true, true, true, true);
    water = pipeline(sceneV, waterF, sceneIn, kHdrFmt, true, true, true, false);
    unlit = pipeline(unlitV, unlitF, unlitIn, kHdrFmt, true, true, true, false);
    sky = pipeline(fullV, skyF, none, kHdrFmt, false, true, false, false);
    composite = pipeline(fullV, compF, none, kOutFmt, false, false, false, false);
    ui = pipeline(uiV, uiF, uiIn, kOutFmt, true, false, false, false);
    for (SDL_GPUShader* s : {sceneV, sceneF, waterF, unlitV, unlitF, fullV, skyF, compF, uiV, uiF}) SDL_ReleaseGPUShader(dev, s);
  }
};

Renderer::Renderer(SDL_Window* window, const RendererOptions& o) : impl_(std::make_unique<Impl>()) {
  Impl& I = *impl_;
  I.window = window;
  I.dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, o.debug, nullptr);
  if (!I.dev) fail("SDL_CreateGPUDevice (é preciso Vulkan)");
  if (window) {
    if (!SDL_ClaimWindowForGPUDevice(I.dev, window)) fail("SDL_ClaimWindowForGPUDevice");
    SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;
    if (!o.vsync) {
      if (SDL_WindowSupportsGPUPresentMode(I.dev, window, SDL_GPU_PRESENTMODE_MAILBOX)) mode = SDL_GPU_PRESENTMODE_MAILBOX;
      else if (SDL_WindowSupportsGPUPresentMode(I.dev, window, SDL_GPU_PRESENTMODE_IMMEDIATE)) mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
    }
    SDL_SetGPUSwapchainParameters(I.dev, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode);
    I.swapFmt = SDL_GetGPUSwapchainTextureFormat(I.dev, window);
  }
  for (SDL_GPUTextureFormat f : {SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREFORMAT_D24_UNORM, SDL_GPU_TEXTUREFORMAT_D16_UNORM}) {
    if (SDL_GPUTextureSupportsFormat(I.dev, f, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
      I.depthFmt = f;
      break;
    }
  }
  I.createPipelines();

  SDL_GPUSamplerCreateInfo si{};
  si.min_filter = SDL_GPU_FILTER_LINEAR;
  si.mag_filter = SDL_GPU_FILTER_LINEAR;
  si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
  si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  I.linear = SDL_CreateGPUSampler(I.dev, &si);
  if (!I.linear) fail("SDL_CreateGPUSampler");

  const MeshData meshes[kPrimCount] = {prim::box(), prim::cylinder(), prim::cone(), prim::sphere(), prim::capsule(0.38, 1.8), prim::ring(0.72, 1.0)};
  for (std::size_t i = 0; i < kPrimCount; ++i) I.prims[i] = I.mesh(meshes[i]);
  const Instance id = makeInstance(glm::mat4(1.0f));
  I.identity = I.staticBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, &id, sizeof id);
  outW_ = o.width;
  outH_ = o.height;
}

Renderer::~Renderer() {
  Impl& I = *impl_;
  if (!I.dev) return;
  SDL_WaitForGPUIdle(I.dev);
  for (auto& m : I.prims) I.release(m);
  for (GpuMap& m : I.maps) {
    if (m.terrainVB) SDL_ReleaseGPUBuffer(I.dev, m.terrainVB);
    if (m.terrainIB) SDL_ReleaseGPUBuffer(I.dev, m.terrainIB);
    if (m.props) SDL_ReleaseGPUBuffer(I.dev, m.props);
    I.release(m.water);
    I.release(m.bridges);
  }
  for (DynBuffer* d : {&I.instances, &I.unlitVerts, &I.uiVerts})
    if (d->buf) SDL_ReleaseGPUBuffer(I.dev, d->buf);
  if (I.identity) SDL_ReleaseGPUBuffer(I.dev, I.identity);
  if (I.staging) SDL_ReleaseGPUTransferBuffer(I.dev, I.staging);
  if (I.download) SDL_ReleaseGPUTransferBuffer(I.dev, I.download);
  for (SDL_GPUTexture* t : {I.hdr, I.depth, I.out, I.atlas})
    if (t) SDL_ReleaseGPUTexture(I.dev, t);
  if (I.linear) SDL_ReleaseGPUSampler(I.dev, I.linear);
  for (SDL_GPUGraphicsPipeline* p : {I.scene, I.water, I.unlit, I.sky, I.composite, I.ui})
    if (p) SDL_ReleaseGPUGraphicsPipeline(I.dev, p);
  if (I.window) SDL_ReleaseWindowFromGPUDevice(I.dev, I.window);
  SDL_DestroyGPUDevice(I.dev);
}

const char* Renderer::driver() const { return SDL_GetGPUDeviceDriver(impl_->dev); }

void Renderer::uploadMap(int mapIndex, const TerrainBuild& terrain, const InstanceLists& props) {
  Impl& I = *impl_;
  GpuMap& m = I.maps[static_cast<std::size_t>(mapIndex)];
  std::vector<Vertex> verts;
  std::vector<std::uint32_t> idx;
  m.chunks.clear();
  for (const TerrainChunk& c : terrain.chunks) {
    ChunkDraw d;
    for (int l = 0; l < 2; ++l) {
      d.vertexOffset[l] = static_cast<Sint32>(verts.size());
      d.firstIndex[l] = static_cast<Uint32>(idx.size());
      d.indexCount[l] = static_cast<Uint32>(c.lod[l].indices.size());
      verts.insert(verts.end(), c.lod[l].vertices.begin(), c.lod[l].vertices.end());
      idx.insert(idx.end(), c.lod[l].indices.begin(), c.lod[l].indices.end());
    }
    d.center = c.center;
    d.radius = c.radius;
    m.chunks.push_back(d);
  }
  m.terrainVB = I.staticBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, verts.data(), static_cast<Uint32>(verts.size() * sizeof(Vertex)));
  m.terrainIB = I.staticBuffer(SDL_GPU_BUFFERUSAGE_INDEX, idx.data(), static_cast<Uint32>(idx.size() * sizeof(std::uint32_t)));
  if (!terrain.water.indices.empty()) m.water = I.mesh(terrain.water);
  if (!terrain.bridges.indices.empty()) m.bridges = I.mesh(terrain.bridges);
  std::vector<Instance> all;
  for (std::size_t p = 0; p < kPrimCount; ++p) {
    m.propFirst[p] = static_cast<Uint32>(all.size());
    m.propCount[p] = static_cast<Uint32>(props.byPrim[p].size());
    all.insert(all.end(), props.byPrim[p].begin(), props.byPrim[p].end());
  }
  m.props = I.staticBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, all.data(), static_cast<Uint32>(all.size() * sizeof(Instance)));
  m.loaded = true;
}

bool Renderer::render(const Frame& f, const std::optional<std::filesystem::path>& capture) {
  Impl& I = *impl_;
  stats_ = {};
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(I.dev);
  if (!cmd) fail("SDL_AcquireGPUCommandBuffer");
  SDL_GPUTexture* swap = nullptr;
  Uint32 sw = 0, sh = 0;
  if (I.window) {
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, I.window, &swap, &sw, &sh)) fail("SDL_WaitAndAcquireGPUSwapchainTexture");
    if (swap) {
      outW_ = static_cast<int>(sw);
      outH_ = static_cast<int>(sh);
    }
  }
  if (outW_ <= 0 || outH_ <= 0) {
    SDL_SubmitGPUCommandBuffer(cmd);
    return false;
  }
  I.ensureTargets(outW_, outH_);

  // ---------------------------------------------------------------- dados dinâmicos
  std::vector<Instance> inst;
  std::array<Uint32, kPrimCount> dynFirst{}, dynCount{};
  if (f.dynamic) {
    for (std::size_t p = 0; p < kPrimCount; ++p) {
      dynFirst[p] = static_cast<Uint32>(inst.size());
      dynCount[p] = static_cast<Uint32>(f.dynamic->byPrim[p].size());
      inst.insert(inst.end(), f.dynamic->byPrim[p].begin(), f.dynamic->byPrim[p].end());
    }
  }
  const Uint32 instBytes = static_cast<Uint32>(inst.size() * sizeof(Instance));
  const Uint32 unlitBytes = f.unlit ? static_cast<Uint32>(f.unlit->size() * sizeof(ColorVertex)) : 0;
  const Uint32 uiBytes = f.ui ? static_cast<Uint32>(f.ui->vertices().size() * sizeof(UiVertex)) : 0;
  const bool atlasUpload = f.atlas && (f.atlas->dirty() || !I.atlas);
  const Uint32 atlasBytes = atlasUpload ? static_cast<Uint32>(f.atlas->pixels().size()) : 0;
  const Uint32 total = instBytes + unlitBytes + uiBytes + atlasBytes;
  if (f.atlas && (!I.atlas || I.atlasSize != f.atlas->size())) {
    if (I.atlas) SDL_ReleaseGPUTexture(I.dev, I.atlas);
    I.atlas = I.texture(SDL_GPU_TEXTUREFORMAT_R8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, f.atlas->size(), f.atlas->size());
    I.atlasSize = f.atlas->size();
  }
  if (total > 0) {
    if (total > I.stagingCap) {
      if (I.staging) SDL_ReleaseGPUTransferBuffer(I.dev, I.staging);
      I.stagingCap = total + total / 2;
      SDL_GPUTransferBufferCreateInfo ti{};
      ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
      ti.size = I.stagingCap;
      I.staging = SDL_CreateGPUTransferBuffer(I.dev, &ti);
      if (!I.staging) fail("SDL_CreateGPUTransferBuffer");
    }
    I.ensureDyn(I.instances, SDL_GPU_BUFFERUSAGE_VERTEX, instBytes);
    I.ensureDyn(I.unlitVerts, SDL_GPU_BUFFERUSAGE_VERTEX, unlitBytes);
    I.ensureDyn(I.uiVerts, SDL_GPU_BUFFERUSAGE_VERTEX, uiBytes);
    auto* dst = static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(I.dev, I.staging, true));
    Uint32 off = 0;
    if (instBytes) std::memcpy(dst + off, inst.data(), instBytes);
    off += instBytes;
    if (unlitBytes) std::memcpy(dst + off, f.unlit->data(), unlitBytes);
    off += unlitBytes;
    if (uiBytes) std::memcpy(dst + off, f.ui->vertices().data(), uiBytes);
    off += uiBytes;
    if (atlasBytes) std::memcpy(dst + off, f.atlas->pixels().data(), atlasBytes);
    SDL_UnmapGPUTransferBuffer(I.dev, I.staging);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    off = 0;
    const auto up = [&](SDL_GPUBuffer* b, Uint32 n) {
      if (!n) return;
      SDL_GPUTransferBufferLocation src{I.staging, off};
      SDL_GPUBufferRegion reg{b, 0, n};
      SDL_UploadToGPUBuffer(cp, &src, &reg, true);
      off += n;
    };
    up(I.instances.buf, instBytes);
    up(I.unlitVerts.buf, unlitBytes);
    up(I.uiVerts.buf, uiBytes);
    if (atlasBytes) {
      SDL_GPUTextureTransferInfo src{I.staging, off, static_cast<Uint32>(I.atlasSize), static_cast<Uint32>(I.atlasSize)};
      SDL_GPUTextureRegion reg{};
      reg.texture = I.atlas;
      reg.w = reg.h = static_cast<Uint32>(I.atlasSize);
      reg.d = 1;
      SDL_UploadToGPUTexture(cp, &src, &reg, false);
      f.atlas->markClean();
    }
    SDL_EndGPUCopyPass(cp);
  }

  // ---------------------------------------------------------------- passe de cena (HDR)
  SDL_GPUColorTargetInfo hdrT{};
  hdrT.texture = I.hdr;
  hdrT.load_op = SDL_GPU_LOADOP_CLEAR;
  hdrT.store_op = SDL_GPU_STOREOP_STORE;
  hdrT.clear_color = {0.05f, 0.07f, 0.1f, 1.0f};
  SDL_GPUDepthStencilTargetInfo dT{};
  dT.texture = I.depth;
  dT.clear_depth = 1.0f;
  dT.load_op = SDL_GPU_LOADOP_CLEAR;
  dT.store_op = SDL_GPU_STOREOP_DONT_CARE;
  dT.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
  dT.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
  SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &hdrT, 1, &dT);
  SDL_PushGPUVertexUniformData(cmd, 0, &f.uniforms, sizeof f.uniforms);
  SDL_PushGPUFragmentUniformData(cmd, 0, &f.uniforms, sizeof f.uniforms);

  SDL_BindGPUGraphicsPipeline(rp, I.sky);
  SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
  ++stats_.drawCalls;

  glm::mat4 vp;
  std::memcpy(&vp[0][0], f.uniforms.viewProj, sizeof(float) * 16);
  const Frustum fr(vp);
  const GpuMap& M = I.maps[static_cast<std::size_t>(f.map)];
  SDL_BindGPUGraphicsPipeline(rp, I.scene);
  if (M.loaded) {
    // relevo
    SDL_GPUBufferBinding vb[2] = {{M.terrainVB, 0}, {I.identity, 0}};
    SDL_BindGPUVertexBuffers(rp, 0, vb, 2);
    SDL_GPUBufferBinding ib{M.terrainIB, 0};
    SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    for (const ChunkDraw& c : M.chunks) {
      if (!fr.sphere(c.center, c.radius)) continue;
      const int l = glm::length(c.center - f.cameraPos) - c.radius < 180.0f ? 0 : 1;
      SDL_DrawGPUIndexedPrimitives(rp, c.indexCount[l], 1, c.firstIndex[l], c.vertexOffset[l], 0);
      ++stats_.drawCalls;
      ++stats_.chunksDrawn;
      stats_.triangles += c.indexCount[l] / 3;
    }
    if (M.bridges.indexCount) {
      SDL_GPUBufferBinding bb[2] = {{M.bridges.vb, 0}, {I.identity, 0}};
      SDL_BindGPUVertexBuffers(rp, 0, bb, 2);
      SDL_GPUBufferBinding bi{M.bridges.ib, 0};
      SDL_BindGPUIndexBuffer(rp, &bi, SDL_GPU_INDEXELEMENTSIZE_32BIT);
      SDL_DrawGPUIndexedPrimitives(rp, M.bridges.indexCount, 1, 0, 0, 0);
      ++stats_.drawCalls;
    }
  }
  const auto drawInstances = [&](SDL_GPUBuffer* buf, const std::array<Uint32, kPrimCount>& first, const std::array<Uint32, kPrimCount>& count) {
    for (std::size_t p = 0; p < kPrimCount; ++p) {
      if (!count[p]) continue;
      SDL_GPUBufferBinding b[2] = {{I.prims[p].vb, 0}, {buf, static_cast<Uint32>(first[p] * sizeof(Instance))}};
      SDL_BindGPUVertexBuffers(rp, 0, b, 2);
      SDL_GPUBufferBinding ib{I.prims[p].ib, 0};
      SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
      SDL_DrawGPUIndexedPrimitives(rp, I.prims[p].indexCount, count[p], 0, 0, 0);
      ++stats_.drawCalls;
      stats_.triangles += static_cast<std::uint64_t>(I.prims[p].indexCount / 3) * count[p];
    }
  };
  if (M.loaded && M.props) drawInstances(M.props, M.propFirst, M.propCount);
  if (instBytes) drawInstances(I.instances.buf, dynFirst, dynCount);

  if (M.loaded && M.water.indexCount) {
    SDL_BindGPUGraphicsPipeline(rp, I.water);
    SDL_GPUBufferBinding wb[2] = {{M.water.vb, 0}, {I.identity, 0}};
    SDL_BindGPUVertexBuffers(rp, 0, wb, 2);
    SDL_GPUBufferBinding wi{M.water.ib, 0};
    SDL_BindGPUIndexBuffer(rp, &wi, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_DrawGPUIndexedPrimitives(rp, M.water.indexCount, 1, 0, 0, 0);
    ++stats_.drawCalls;
  }
  if (unlitBytes) {
    SDL_BindGPUGraphicsPipeline(rp, I.unlit);
    SDL_GPUBufferBinding ub{I.unlitVerts.buf, 0};
    SDL_BindGPUVertexBuffers(rp, 0, &ub, 1);
    SDL_DrawGPUPrimitives(rp, static_cast<Uint32>(f.unlit->size()), 1, 0, 0);
    ++stats_.drawCalls;
  }
  SDL_EndGPURenderPass(rp);

  // ---------------------------------------------------------------- saída: composição + interface
  SDL_GPUColorTargetInfo outT{};
  outT.texture = I.out;
  outT.load_op = SDL_GPU_LOADOP_DONT_CARE;
  outT.store_op = SDL_GPU_STOREOP_STORE;
  rp = SDL_BeginGPURenderPass(cmd, &outT, 1, nullptr);
  const float post[4] = {f.exposure, 0, 0, 0};
  SDL_PushGPUFragmentUniformData(cmd, 0, post, sizeof post);
  SDL_BindGPUGraphicsPipeline(rp, I.composite);
  SDL_GPUTextureSamplerBinding sceneTex{I.hdr, I.linear};
  SDL_BindGPUFragmentSamplers(rp, 0, &sceneTex, 1);
  SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
  ++stats_.drawCalls;
  if (uiBytes && I.atlas) {
    const float screen[4] = {static_cast<float>(outW_), static_cast<float>(outH_), 0, 0};
    SDL_PushGPUVertexUniformData(cmd, 0, screen, sizeof screen);
    SDL_BindGPUGraphicsPipeline(rp, I.ui);
    SDL_GPUBufferBinding ub{I.uiVerts.buf, 0};
    SDL_BindGPUVertexBuffers(rp, 0, &ub, 1);
    SDL_GPUTextureSamplerBinding at{I.atlas, I.linear};
    SDL_BindGPUFragmentSamplers(rp, 0, &at, 1);
    SDL_DrawGPUPrimitives(rp, static_cast<Uint32>(f.ui->vertices().size()), 1, 0, 0);
    ++stats_.drawCalls;
  }
  SDL_EndGPURenderPass(rp);

  // ---------------------------------------------------------------- janela e captura
  if (swap) {
    SDL_GPUBlitInfo bi{};
    bi.source.texture = I.out;
    bi.source.w = static_cast<Uint32>(outW_);
    bi.source.h = static_cast<Uint32>(outH_);
    bi.destination.texture = swap;
    bi.destination.w = sw;
    bi.destination.h = sh;
    bi.load_op = SDL_GPU_LOADOP_DONT_CARE;
    bi.filter = SDL_GPU_FILTER_NEAREST;
    SDL_BlitGPUTexture(cmd, &bi);
  }
  if (!capture) {
    SDL_SubmitGPUCommandBuffer(cmd);
    return true;
  }
  const Uint32 bytes = static_cast<Uint32>(outW_ * outH_ * 4);
  if (bytes > I.downloadCap) {
    if (I.download) SDL_ReleaseGPUTransferBuffer(I.dev, I.download);
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    ti.size = bytes;
    I.download = SDL_CreateGPUTransferBuffer(I.dev, &ti);
    if (!I.download) fail("SDL_CreateGPUTransferBuffer (download)");
    I.downloadCap = bytes;
  }
  SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureRegion reg{};
  reg.texture = I.out;
  reg.w = static_cast<Uint32>(outW_);
  reg.h = static_cast<Uint32>(outH_);
  reg.d = 1;
  SDL_GPUTextureTransferInfo dst{I.download, 0, static_cast<Uint32>(outW_), static_cast<Uint32>(outH_)};
  SDL_DownloadFromGPUTexture(cp, &reg, &dst);
  SDL_EndGPUCopyPass(cp);
  SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
  if (!fence) fail("SDL_SubmitGPUCommandBufferAndAcquireFence");
  SDL_WaitForGPUFences(I.dev, true, &fence, 1);
  SDL_ReleaseGPUFence(I.dev, fence);
  const auto* px = static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(I.dev, I.download, false));
  std::vector<std::uint8_t> rgba(px, px + bytes);
  SDL_UnmapGPUTransferBuffer(I.dev, I.download);
  for (std::size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
  return writePng(*capture, rgba, outW_, outH_);
}

}  // namespace rpg::client
