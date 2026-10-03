#pragma once
// Estado interno do Renderer (compartilhado entre Renderer.cpp e RendererPack.cpp).
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include <SDL3/SDL.h>

#include "client/geom/Primitives.h"
#include "client/render/Renderer.h"
#include "client/render/ShaderBlobs.h"
#include "client/world/PackMaterials.h"

namespace rpg::client {

[[noreturn]] inline void gpuFail(const std::string& what) { throw std::runtime_error(what + ": " + SDL_GetError()); }

namespace detail {

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

}  // namespace detail

using namespace detail;

// Pacote visual na GPU.
struct PackGpu {
  struct Tex {
    SDL_GPUTexture* tex = nullptr;
    SDL_GPUSampler* sampler = nullptr;
    bool array = false;
  };
  struct Mat {
    MaterialSetup setup;
    std::array<SDL_GPUTextureSamplerBinding, 7> bindings{};
    Uint32 bindingCount = 0;
  };
  using PipelineKey = std::tuple<bool, int, int, bool, bool, bool, float, float, bool, bool>;

  bool loaded = false;
  SDL_GPUBuffer *vb = nullptr, *ib = nullptr, *staticInst = nullptr, *skinVB = nullptr;
  std::vector<MeshSlice> geometries;
  std::vector<TerrainSlice> terrain;
  std::vector<Tex> textures;
  std::vector<Mat> materials;
  SDL_GPUTexture *white = nullptr, *flatNormal = nullptr, *dfg = nullptr, *env = nullptr, *whiteArray = nullptr;
  SDL_GPUSampler *clampLinear = nullptr, *envSampler = nullptr;
  std::map<std::tuple<int, int, bool, bool, int>, SDL_GPUSampler*> samplers;
  std::map<PipelineKey, SDL_GPUGraphicsPipeline*> pipelines;
  SDL_GPUShader *meshV = nullptr, *skinnedV = nullptr, *surfaceF = nullptr, *terrainF = nullptr;
  DynBuffer frameInst, bones;
};

struct Renderer::Impl {
  SDL_Window* window = nullptr;
  SDL_GPUDevice* dev = nullptr;
  SDL_GPUTextureFormat swapFmt = SDL_GPU_TEXTUREFORMAT_INVALID;
  SDL_GPUTextureFormat depthFmt = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
  static constexpr SDL_GPUTextureFormat kHdrFmt = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
  static constexpr SDL_GPUTextureFormat kOutFmt = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

  SDL_GPUGraphicsPipeline *scene = nullptr, *water = nullptr, *unlit = nullptr, *sky = nullptr, *composite = nullptr, *ui = nullptr;
  SDL_GPUGraphicsPipeline *stars = nullptr, *lines = nullptr;
  SDL_GPUSampler* linear = nullptr;
  SDL_GPUTexture *hdr = nullptr, *depth = nullptr, *out = nullptr, *atlas = nullptr;
  int texW = 0, texH = 0, atlasSize = 0;
  std::uint32_t atlasGeneration = ~0u;

  std::array<GpuMesh, kPrimCount> prims;
  SDL_GPUBuffer* identity = nullptr;
  std::array<GpuMap, 2> maps;
  DynBuffer instances, unlitVerts, uiVerts, skyStars, skyLines, skyOverlay;
  SDL_GPUTransferBuffer* staging = nullptr;
  Uint32 stagingCap = 0;
  SDL_GPUTransferBuffer* download = nullptr;
  Uint32 downloadCap = 0;
  PackGpu pack;
  // Sem janela nada limita a CPU: até 2 quadros em voo (senão a fila cresce sem fim no lavapipe).
  std::array<SDL_GPUFence*, 2> inFlight{};
  std::size_t frameSlot = 0;

  // RendererPack.cpp
  void releasePack();
  SDL_GPUSampler* packSampler(Wrap s, Wrap t, bool nearest, bool mips, int anisotropy);
  SDL_GPUTexture* uploadTexture(SDL_GPUTextureType type, SDL_GPUTextureFormat fmt, int w, int h, int layers, int levels,
                                const std::vector<std::vector<const std::uint8_t*>>& data, int bytesPerPixel, bool generateMips);
  SDL_GPUGraphicsPipeline* packPipeline(const MaterialSetup& m, bool mirrored, bool skinned);
  void drawPack(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* rp, const PackFrame& f, bool transparent, FrameStats& stats);

  SDL_GPUShader* shader(const char* name, SDL_GPUShaderStage stage, Uint32 samplers, Uint32 uniforms, Uint32 storageBuffers = 0) {
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
    ci.num_storage_buffers = storageBuffers;
    SDL_GPUShader* s = SDL_CreateGPUShader(dev, &ci);
    if (!s) gpuFail(std::string("SDL_CreateGPUShader(") + name + ")");
    return s;
  }

  SDL_GPUBuffer* buffer(SDL_GPUBufferUsageFlags usage, Uint32 size) {
    SDL_GPUBufferCreateInfo ci{};
    ci.usage = usage;
    ci.size = std::max<Uint32>(size, 16);
    SDL_GPUBuffer* b = SDL_CreateGPUBuffer(dev, &ci);
    if (!b) gpuFail("SDL_CreateGPUBuffer");
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
    if (!tb) gpuFail("SDL_CreateGPUTransferBuffer");
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
    if (!t) gpuFail("SDL_CreateGPUTexture");
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
                                    SDL_GPUPrimitiveType prim = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, bool additive = false) {
    SDL_GPUColorTargetDescription ct{};
    ct.format = color;
    if (blend) {
      ct.blend_state.enable_blend = true;
      ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
      ct.blend_state.dst_color_blendfactor = additive ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
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
    if (!p) gpuFail("SDL_CreateGPUGraphicsPipeline");
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
    SDL_GPUShader* starsV = shader("stars.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader* starsF = shader("stars.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
    SDL_GPUShader* compF = shader("composite.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    SDL_GPUShader* uiV = shader("ui.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader* uiF = shader("ui.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);

    scene = pipeline(sceneV, sceneF, sceneIn, kHdrFmt, true, true, true, true);
    water = pipeline(sceneV, waterF, sceneIn, kHdrFmt, true, true, true, false);
    unlit = pipeline(unlitV, unlitF, unlitIn, kHdrFmt, true, true, true, false);
    sky = pipeline(fullV, skyF, none, kHdrFmt, false, true, false, false);
    const SDL_GPUVertexBufferDescription starBuf{0, 32, SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0};
    const SDL_GPUVertexAttribute starAttrs[2] = {{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0}, {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 16}};
    const SDL_GPUVertexInputState starIn{&starBuf, 1, starAttrs, 2};
    stars = pipeline(starsV, starsF, starIn, kHdrFmt, true, true, true, false, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, true);
    lines = pipeline(unlitV, unlitF, unlitIn, kHdrFmt, true, true, true, false, SDL_GPU_PRIMITIVETYPE_LINELIST);
    composite = pipeline(fullV, compF, none, kOutFmt, false, false, false, false);
    ui = pipeline(uiV, uiF, uiIn, kOutFmt, true, false, false, false);
    for (SDL_GPUShader* s : {sceneV, sceneF, waterF, unlitV, unlitF, fullV, skyF, compF, uiV, uiF, starsV, starsF}) SDL_ReleaseGPUShader(dev, s);
  }
};

}  // namespace rpg::client
