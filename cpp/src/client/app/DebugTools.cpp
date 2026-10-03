#include "client/app/DebugTools.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

namespace rpg::client {

namespace proto = protocol;

DebugTools::DebugTools(SDL_Window* window, SDL_GPUDevice* device, SDL_GPUTextureFormat target) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // nada de imgui.ini ao lado do executável
  ImGui::StyleColorsDark();
  ImGui_ImplSDL3_InitForSDLGPU(window);
  ImGui_ImplSDLGPU3_InitInfo info;
  info.Device = device;
  info.ColorTargetFormat = target;
  info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
  ImGui_ImplSDLGPU3_Init(&info);
}

DebugTools::~DebugTools() {
  ImGui_ImplSDLGPU3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
}

void DebugTools::event(const SDL_Event& e) { ImGui_ImplSDL3_ProcessEvent(&e); }

bool DebugTools::wantsMouse() const { return open_ && ImGui::GetIO().WantCaptureMouse; }
bool DebugTools::wantsKeyboard() const { return open_ && ImGui::GetIO().WantCaptureKeyboard; }

DebugTools::Output DebugTools::frame(const Info& in, const std::vector<std::string>& views) {
  Output out;
  built_ = false;
  if (!open_) return out;
  ImGui_ImplSDLGPU3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  ImGui::SetNextWindowPos(ImVec2(12, 120), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(340, 520), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Depuração (F3)", &open_)) {
    ImGui::Text("%.1f ms  ·  %.0f fps", in.frameMs, in.fps);
    ImGui::Text("desenhos %d  ·  instâncias %d  ·  entidades %d", in.drawCalls, in.instances, in.entities);
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Jogador", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Text("%s  (%.1f, %.1f, %.1f)  yaw %.2f", in.map.c_str(), in.x, in.y, in.z, in.yaw);
      ImGui::Text("estado %s", in.state.c_str());
      ImGui::Text("%s · %s", in.zone.c_str(), in.place.c_str());
      ImGui::Text("vida %.0f / %.0f  ·  vigor %.0f  ·  %.0f moedas", in.hp, in.maxHp, in.vigor, in.coins);
      ImGui::Text("%s", in.clock.c_str());
    }
    if (ImGui::CollapsingHeader("Câmera", ImGuiTreeNodeFlags_DefaultOpen)) {
      bool free = in.freeCam;
      if (ImGui::Checkbox("Câmera livre nos enquadramentos", &free)) out.toggleFreeCam = true;
      static int sel = 0;
      if (!views.empty()) {
        sel = std::min(sel, static_cast<int>(views.size()) - 1);
        if (ImGui::BeginCombo("Enquadramento", views[static_cast<std::size_t>(sel)].c_str())) {
          for (int i = 0; i < static_cast<int>(views.size()); ++i)
            if (ImGui::Selectable(views[static_cast<std::size_t>(i)].c_str(), i == sel)) sel = i;
          ImGui::EndCombo();
        }
        if (ImGui::Button("Ir ao enquadramento")) out.view = views[static_cast<std::size_t>(sel)];
      }
    }
    if (ImGui::CollapsingHeader("Comandos de desenvolvimento", ImGuiTreeNodeFlags_DefaultOpen)) {
      const auto dev = [&](proto::DevCommand c, double a = 0, double b = 0) { out.requests.push_back(proto::ReqDev{c, a, b, 0}); };
      ImGui::SliderFloat("moedas", &coins_, 10, 1000, "%.0f");
      if (ImGui::Button("Dar moedas")) dev(proto::DevCommand::Coins, static_cast<double>(coins_));
      ImGui::SliderFloat("dano", &hurt_, 1, 200, "%.0f");
      if (ImGui::Button("Ferir")) dev(proto::DevCommand::Hurt, static_cast<double>(hurt_));
      if (ImGui::Button("Abrir portal perto")) dev(proto::DevCommand::Portal);
      ImGui::SameLine();
      if (ImGui::Button("Derrotar próximos")) dev(proto::DevCommand::KillNear);
      if (ImGui::Button("Contribuir com o canteiro")) dev(proto::DevCommand::Contribute, 20);
      if (ImGui::Button("Noite")) dev(proto::DevCommand::Night);
      ImGui::SameLine();
      if (ImGui::Button("Sumir uma estrela")) dev(proto::DevCommand::Vanish);
    }
  }
  ImGui::End();
  ImGui::Render();
  built_ = true;
  return out;
}

void DebugTools::render(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target) {
  if (!built_) return;
  ImDrawData* dd = ImGui::GetDrawData();
  if (!dd || dd->CmdListsCount == 0) return;
  ImGui_ImplSDLGPU3_PrepareDrawData(dd, cmd);
  SDL_GPUColorTargetInfo ct{};
  ct.texture = target;
  ct.load_op = SDL_GPU_LOADOP_LOAD;
  ct.store_op = SDL_GPU_STOREOP_STORE;
  SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
  ImGui_ImplSDLGPU3_RenderDrawData(dd, cmd, rp);
  SDL_EndGPURenderPass(rp);
}

}  // namespace rpg::client
