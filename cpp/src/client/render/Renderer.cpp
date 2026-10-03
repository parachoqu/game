#include "client/render/Renderer.h"

#include <algorithm>
#include <cstring>

#include "client/image/PngWriter.h"
#include "client/render/RendererImpl.h"

namespace rpg::client {

Renderer::Renderer(SDL_Window* window, const RendererOptions& o) : impl_(std::make_unique<Impl>()) {
  Impl& I = *impl_;
  I.window = window;
  I.dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, o.debug, nullptr);
  if (!I.dev) gpuFail("SDL_CreateGPUDevice (é preciso Vulkan)");
  if (window) {
    if (!SDL_ClaimWindowForGPUDevice(I.dev, window)) gpuFail("SDL_ClaimWindowForGPUDevice");
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
  if (!I.linear) gpuFail("SDL_CreateGPUSampler");

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
  for (SDL_GPUFence*& fence : I.inFlight)
    if (fence) SDL_ReleaseGPUFence(I.dev, fence);
  for (auto& m : I.prims) I.release(m);
  for (GpuMap& m : I.maps) {
    if (m.terrainVB) SDL_ReleaseGPUBuffer(I.dev, m.terrainVB);
    if (m.terrainIB) SDL_ReleaseGPUBuffer(I.dev, m.terrainIB);
    if (m.props) SDL_ReleaseGPUBuffer(I.dev, m.props);
    I.release(m.water);
    I.release(m.bridges);
  }
  for (DynBuffer* d : {&I.instances, &I.unlitVerts, &I.uiVerts, &I.skyStars, &I.skyLines, &I.skyOverlay})
    if (d->buf) SDL_ReleaseGPUBuffer(I.dev, d->buf);
  I.releasePack();
  if (I.identity) SDL_ReleaseGPUBuffer(I.dev, I.identity);
  if (I.staging) SDL_ReleaseGPUTransferBuffer(I.dev, I.staging);
  if (I.download) SDL_ReleaseGPUTransferBuffer(I.dev, I.download);
  for (SDL_GPUTexture* t : {I.hdr, I.depth, I.out, I.atlas})
    if (t) SDL_ReleaseGPUTexture(I.dev, t);
  if (I.linear) SDL_ReleaseGPUSampler(I.dev, I.linear);
  for (SDL_GPUGraphicsPipeline* p : {I.scene, I.water, I.unlit, I.sky, I.composite, I.ui, I.stars, I.lines})
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
  if (SDL_GPUFence*& fence = I.inFlight[I.frameSlot]) {
    SDL_WaitForGPUFences(I.dev, true, &fence, 1);
    SDL_ReleaseGPUFence(I.dev, fence);
    fence = nullptr;
  }
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(I.dev);
  if (!cmd) gpuFail("SDL_AcquireGPUCommandBuffer");
  SDL_GPUTexture* swap = nullptr;
  Uint32 sw = 0, sh = 0;
  if (I.window) {
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, I.window, &swap, &sw, &sh)) gpuFail("SDL_WaitAndAcquireGPUSwapchainTexture");
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
  const bool packOn = I.pack.loaded && f.pack;
  const Uint32 packInstBytes = packOn ? static_cast<Uint32>(f.pack->instances.size() * sizeof(Instance)) : 0;
  const Uint32 boneBytes = packOn ? static_cast<Uint32>(f.pack->bones.size() * sizeof(glm::mat4)) : 0;
  const Uint32 starBytes = f.sky ? static_cast<Uint32>(f.sky->stars.size() * sizeof(StarInstance)) : 0;
  const Uint32 lineBytes = f.sky ? static_cast<Uint32>(f.sky->lines.size() * sizeof(ColorVertex)) : 0;
  const Uint32 overlayBytes = f.sky ? static_cast<Uint32>(f.sky->overlay.size() * sizeof(ColorVertex)) : 0;
  const Uint32 total = instBytes + unlitBytes + uiBytes + atlasBytes + packInstBytes + boneBytes + starBytes + lineBytes + overlayBytes;
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
      if (!I.staging) gpuFail("SDL_CreateGPUTransferBuffer");
    }
    I.ensureDyn(I.instances, SDL_GPU_BUFFERUSAGE_VERTEX, instBytes);
    I.ensureDyn(I.unlitVerts, SDL_GPU_BUFFERUSAGE_VERTEX, unlitBytes);
    I.ensureDyn(I.uiVerts, SDL_GPU_BUFFERUSAGE_VERTEX, uiBytes);
    I.ensureDyn(I.pack.frameInst, SDL_GPU_BUFFERUSAGE_VERTEX, packInstBytes);
    I.ensureDyn(I.pack.bones, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, boneBytes);
    I.ensureDyn(I.skyStars, SDL_GPU_BUFFERUSAGE_VERTEX, starBytes);
    I.ensureDyn(I.skyLines, SDL_GPU_BUFFERUSAGE_VERTEX, lineBytes);
    I.ensureDyn(I.skyOverlay, SDL_GPU_BUFFERUSAGE_VERTEX, overlayBytes);
    auto* dst = static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(I.dev, I.staging, true));
    Uint32 off = 0;
    if (instBytes) std::memcpy(dst + off, inst.data(), instBytes);
    off += instBytes;
    if (unlitBytes) std::memcpy(dst + off, f.unlit->data(), unlitBytes);
    off += unlitBytes;
    if (uiBytes) std::memcpy(dst + off, f.ui->vertices().data(), uiBytes);
    off += uiBytes;
    if (atlasBytes) std::memcpy(dst + off, f.atlas->pixels().data(), atlasBytes);
    off += atlasBytes;
    if (packInstBytes) std::memcpy(dst + off, f.pack->instances.data(), packInstBytes);
    off += packInstBytes;
    if (boneBytes) std::memcpy(dst + off, f.pack->bones.data(), boneBytes);
    off += boneBytes;
    if (starBytes) std::memcpy(dst + off, f.sky->stars.data(), starBytes);
    off += starBytes;
    if (lineBytes) std::memcpy(dst + off, f.sky->lines.data(), lineBytes);
    off += lineBytes;
    if (overlayBytes) std::memcpy(dst + off, f.sky->overlay.data(), overlayBytes);
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
    off += atlasBytes;
    up(I.pack.frameInst.buf, packInstBytes);
    up(I.pack.bones.buf, boneBytes);
    up(I.skyStars.buf, starBytes);
    up(I.skyLines.buf, lineBytes);
    up(I.skyOverlay.buf, overlayBytes);
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
  if (packOn) I.drawPack(cmd, rp, *f.pack, false, stats_);
  SDL_BindGPUGraphicsPipeline(rp, I.scene);
  if (M.loaded && !packOn) {
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
  if (M.loaded && M.props && !packOn) drawInstances(M.props, M.propFirst, M.propCount);
  if (instBytes) drawInstances(I.instances.buf, dynFirst, dynCount);
  // céu noturno: lua e anel da lacuna, estrelas (aditivas), linhas das constelações
  if (overlayBytes) {
    SDL_BindGPUGraphicsPipeline(rp, I.unlit);
    SDL_GPUBufferBinding ob{I.skyOverlay.buf, 0};
    SDL_BindGPUVertexBuffers(rp, 0, &ob, 1);
    SDL_DrawGPUPrimitives(rp, static_cast<Uint32>(f.sky->overlay.size()), 1, 0, 0);
    ++stats_.drawCalls;
  }
  if (starBytes) {
    SDL_BindGPUGraphicsPipeline(rp, I.stars);
    SDL_GPUBufferBinding sb{I.skyStars.buf, 0};
    SDL_BindGPUVertexBuffers(rp, 0, &sb, 1);
    SDL_DrawGPUPrimitives(rp, 6, static_cast<Uint32>(f.sky->stars.size()), 0, 0);
    ++stats_.drawCalls;
  }
  if (lineBytes) {
    SDL_BindGPUGraphicsPipeline(rp, I.lines);
    SDL_GPUBufferBinding lb{I.skyLines.buf, 0};
    SDL_BindGPUVertexBuffers(rp, 0, &lb, 1);
    SDL_DrawGPUPrimitives(rp, static_cast<Uint32>(f.sky->lines.size()), 1, 0, 0);
    ++stats_.drawCalls;
  }
  if (packOn) I.drawPack(cmd, rp, *f.pack, true, stats_);

  if (M.loaded && M.water.indexCount && !packOn) {
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
    I.inFlight[I.frameSlot] = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    I.frameSlot = (I.frameSlot + 1) % I.inFlight.size();
    return true;
  }
  const Uint32 bytes = static_cast<Uint32>(outW_ * outH_ * 4);
  if (bytes > I.downloadCap) {
    if (I.download) SDL_ReleaseGPUTransferBuffer(I.dev, I.download);
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    ti.size = bytes;
    I.download = SDL_CreateGPUTransferBuffer(I.dev, &ti);
    if (!I.download) gpuFail("SDL_CreateGPUTransferBuffer (download)");
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
  if (!fence) gpuFail("SDL_SubmitGPUCommandBufferAndAcquireFence");
  SDL_WaitForGPUFences(I.dev, true, &fence, 1);
  SDL_ReleaseGPUFence(I.dev, fence);
  const auto* px = static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(I.dev, I.download, false));
  std::vector<std::uint8_t> rgba(px, px + bytes);
  SDL_UnmapGPUTransferBuffer(I.dev, I.download);
  for (std::size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
  return writePng(*capture, rgba, outW_, outH_);
}

}  // namespace rpg::client
