#include "client/app/ClientApp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>

#include <glm/gtc/matrix_transform.hpp>

#include "client/ClientWorld.h"
#include "client/Input.h"
#include "client/Presentation.h"
#include "client/assets/EnvironmentMap.h"
#include "client/assets/Image.h"
#include "client/assets/ScenePack.h"
#include "client/entities/CharacterViews.h"
#include "client/fx/FxState.h"
#include "client/game/Aim.h"
#include "client/game/CommandBuilder.h"
#include "client/game/Lighting.h"
#include "client/game/Targets.h"
#include "client/game/ThirdPersonCamera.h"
#include "client/geom/SceneBatch.h"
#include "client/geom/TerrainMesh.h"
#include "client/net/ClientSession.h"
#include "client/platform/Platform.h"
#include "client/render/Renderer.h"
#include "client/ui/FontAtlas.h"
#include "client/ui/Hud.h"
#include "client/ui/Localization.h"
#include "client/ui/GameUi.h"
#include "client/ui/UiBatch.h"
#include "client/ui/UiSystem.h"
#include "client/world/EntityModels.h"
#include "client/world/PackScene.h"
#include "client/world/SkyView.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/data/GameData.h"
#include "core/protocol/Replicated.h"
#include "core/world/StaticWorld.h"

namespace rpg::client {

namespace proto = protocol;

namespace {

struct ViewDef {
  const char* name;
  glm::dvec3 pos, look;
  bool turbulent = false;
};

// tools/capture-demo-views.mjs
const ViewDef kViews[] = {
    {"02_mercado", {40, 24, 405}, {40, 18, 375}},
    {"03_ponte_principal", {-15, 23, 120}, {0, 18, 145}},
    {"04_passagem_garganta", {-25, 30, -50}, {-25, 24, -95}},
    {"05_bosque_forjas", {-70, 30, -80}, {-120, 24, -80}},
    {"06_mina", {-315, 32, 72}, {-350, 24, 72}},
    {"07_observatorio", {-235, 116, -205}, {-265, 110, -205}},
    {"08_entreposto_norte", {35, 36, -395}, {35, 29, -435}},
    {"09_campos", {110, 28, 290}, {145, 20, 240}},
    {"10_margens_rio", {80, 22, 145}, {25, 16, 145}},
    {"11_ermos", {-315, 48, -335}, {-360, 34, -375}},
    {"12_portal_turbulenta", {295, 31, -295}, {315, 24, -320}},
    {"12b_turbulenta_interior", {1365, 30, -30}, {1400, 24, 0}, true},
    {"13_vista_elevada_horizonte", {-260, 140, -180}, {60, 25, 180}},
};

const ViewDef* findView(const std::string& n) {
  for (const ViewDef& v : kViews)
    if (n == v.name) return &v;
  return nullptr;
}

enum class Mode : std::uint8_t { Loading, Title, Create, Joining, Game, Failed };

}  // namespace

bool ClientApp::knownView(const std::string& name) {
  return name == "01_title_screen" || name == "01b_create_screen" || name == "14_hud" || name == "15_inventario" || name == "16_livro" ||
         name == "17_mapa" || findView(name) != nullptr;
}

struct ClientApp::Impl {
  const GameData& data;
  const StaticWorld& statics;
  AppOptions opt;
  Platform platform;
  Renderer renderer;
  FontAtlas atlas;
  UiBatch ui;
  UiFonts fonts;
  Localization L;
  Presentation look;
  ClientSession session;
  ClientWorld world;
  FxState fx;
  ThirdPersonCamera cam;
  CommandBuilder commands;
  Hud hud;
  std::unique_ptr<UiSystem> rmlUi;  // index.html da demo em RmlUi (assets/ui)
  std::unique_ptr<GameUi> gameUi;
  CreationForm form;
  InputState in;
  Mode mode = Mode::Loading;
  int loadStep = 0, bootPhase = -1, bootSubs = 0;
  double titleT = 0, lastCmdSent = -1;
  bool tabChord = false, paused = false, snapCamera = true, viewTeleported = false;
  int gameFrames = 0, totalFrames = 0;
  bool deathShown = false, eventShown = false, introShown = false, observing = false;
  std::string quality = "alta";
  bool muted = false;
  std::string failure;
  InstanceLists dynamic;
  std::vector<ColorVertex> unlit;
  std::array<bool, 2> mapUploaded{};
  // pacote visual (cpp/assets/client); sem ele, o cenário grey-box da fase 3
  std::optional<ScenePack> pack;
  PackMeshes packMeshes;
  std::unique_ptr<WorldView> worldView;
  std::unique_ptr<CharacterLibrary> characters;
  std::unique_ptr<CharacterViews> characterViews;
  PackFrame packFrame;
  DynamicSceneState dynScene;
  EnvironmentSelector env;
  DayPalette palette;
  SkyView skyView;
  SkyFrame skyFrame;

  Impl(const GameData& d, const StaticWorld& s, AppOptions o, std::unique_ptr<net::ITransport> t)
      : data(d),
        statics(s),
        opt(std::move(o)),
        platform(PlatformOptions{opt.width, opt.height, opt.fullscreen, opt.headless, "Projeto Game"}),
        renderer(platform.window(), RendererOptions{opt.width, opt.height, false, opt.vsync}),
        atlas(2048),
        ui(atlas),
        L(Localization::load(opt.dataDir / "text" / "pt-BR", d)),
        look(Presentation::load(opt.dataDir / "presentation")),
        session(std::move(t)),
        fx(L) {
    fonts.display = atlas.addFace(opt.assetsDir / "fonts" / "Fraunces-SemiBold.ttf");
    fonts.body = atlas.addFace(opt.assetsDir / "fonts" / "DejaVuSans.ttf");
    fonts.bold = atlas.addFace(opt.assetsDir / "fonts" / "DejaVuSans-Bold.ttf");
    std::random_device rd;
    form.name = kSuggestedNames[opt.view ? 0 : rd() % kSuggestedNames.size()];  // capturas: sempre o primeiro
    in.width = opt.width;
    in.height = opt.height;
    log::info("GPU: {}", renderer.driver());
    rmlUi = std::make_unique<UiSystem>(UiSystem::Options{opt.assetsDir / "ui", opt.assetsDir / "fonts", opt.width, opt.height});
    gameUi = std::make_unique<GameUi>(*rmlUi, L, data, look);
    // main.js PHASES: as mesmas fases e pesos da barra
    gameUi->setBootPhases({{"texturas", 0.28}, {"modelos", 0.18}, {"mundo", 0.26}, {"construções e floresta", 0.14}, {"sombreadores", 0.14}});
  }

  // Interface RmlUi deste quadro: tamanho, input, animações e o desenho gravado.
  const RmlRender* drawUi(double now) {
    rmlUi->resize(in.width, in.height);
    rmlUi->input(in);
    rmlUi->update(now);
    rmlUi->draw();
    return &rmlUi->render();
  }

  void render(Renderer::Frame& f, const std::optional<std::filesystem::path>& capture) {
    f.rml = drawUi(platform.now());
    renderer.render(f, capture);
    rmlUi->render().endFrame();
  }

  const StaticMap& mapOf(MapKind k) const { return statics.map(k); }

  // Constrói e envia um mapa à GPU (relevo, água, pontes, cenário estático).
  void loadMap(MapKind k) {
    const StaticMap& m = mapOf(k);
    const TerrainBuild t = buildTerrain(m);
    const InstanceLists props = buildStaticProps(m, statics.layout());
    renderer.uploadMap(static_cast<int>(k), t, props);
    mapUploaded[static_cast<std::size_t>(k)] = true;
    log::info("mapa {}: {} blocos de relevo, {} objetos de cenário", k == MapKind::Region ? "região" : "Turbulenta",
              t.chunks.size(), props.total());
  }

  // Pacote visual em etapas (a tela de carregamento anda entre elas): tabelas, binário e texturas;
  // moldes dos personagens; luz de ambiente; malhas e GPU. Sem ele, o cenário grey-box da fase 3.
  void packStage(int stage) {
    if (stage > 0 && !pack) return;
    const std::filesystem::path dir = opt.assetsDir / "client";
    try {
      if (stage == 0) {
        pack.emplace(ScenePack::load(dir));
      } else if (stage == 1) {
        // moldes dos personagens e as variantes de material (entram no pacote antes da GPU)
        if (!pack->rigs.empty()) characters = std::make_unique<CharacterLibrary>(*pack);
      } else if (stage == 2) {
        if (pack->environment.present) {
          env.setKit(decodeRgbe(loadWebp(pack->dir / pathFromUtf8(pack->environment.file))));
          palette.calibrate(pack->environment.zenith, pack->environment.horizon, pack->environment.ground);
        }
      } else {
        packMeshes = buildPackMeshes(*pack, statics);
        worldView = std::make_unique<WorldView>(*pack, packMeshes);
        renderer.loadPack(*pack, packMeshes, worldView->staticInstances());
        if (characters) characterViews = std::make_unique<CharacterViews>(*pack, *characters);
      }
    } catch (const std::exception& e) {
      log::warn("pacote visual indisponível ({}); usando o cenário grey-box", e.what());
      characterViews.reset();
      characters.reset();
      pack.reset();
      worldView.reset();
    }
  }

  // Estrelas, lua e constelações (sky.js) para este quadro; acerta os uniformes do céu.
  const SkyFrame* skyFor(FrameUniforms& u, const ViewCamera& v, double hour, bool turbulent, double skyTime,
                         std::vector<SkyInput::Gone> vanished) {
    const SceneLighting Lg = lightingAt(hour, turbulent, palette);
    SkyInput si;
    si.camPos = glm::vec3(v.position);
    si.sunDir = Lg.sunDir;
    si.turbulent = turbulent;
    si.night = Lg.daylight.night;
    si.time = skyTime;
    si.vanished = std::move(vanished);
    skyView.build(si, skyFrame);
    u.sky[0] = static_cast<float>(skyTime);
    u.sky[1] = skyFrame.starNight;
    u.sky[2] = skyFrame.lineOpacity;
    u.sky[3] = 1.0f;
    return &skyFrame;
  }

  // Cenário do pacote para a câmera deste quadro.
  const PackFrame* packFor(const ViewCamera& v, double gameTime) {
    if (!worldView || !renderer.packLoaded()) return nullptr;
    const glm::vec3 eye(v.position);
    const glm::vec3 fwd = glm::normalize(glm::vec3(v.target - v.position));
    dynScene.time = gameTime;
    worldView->update(eye, glm::mat4(v.viewProj()), fwd, dynScene, packFrame);
    return &packFrame;
  }

  proto::Hello hello() const {
    proto::Hello h;
    h.name = opt.autoStart && !opt.name.empty() ? opt.name : form.name;
    if (h.name.empty()) h.name = L.ui("create.defaultName");
    h.origin = opt.autoStart && !opt.origin.empty() ? opt.origin : data.origins.key(OriginId{static_cast<std::uint16_t>(form.origin)});
    h.start = opt.autoStart && !opt.start.empty() ? opt.start : data.starts.key(StartId{static_cast<std::uint16_t>(form.start)});
    h.model = opt.autoStart && !opt.model.empty() ? opt.model : kCharacterModels[form.model];
    return h;
  }

  void join() {
    session.join(hello());
    mode = Mode::Joining;
    snapCamera = true;
  }

  // ---------------------------------------------------------------- luz e uniformes
  FrameUniforms uniforms(const ViewCamera& v, double hour, bool turbulent, double time, const glm::dvec3* occTarget = nullptr) {
    const SceneLighting Lg = lightingAt(hour, turbulent, palette);
    FrameUniforms u{};
    const glm::mat4 vp = glm::mat4(v.viewProj());
    FrameUniforms::put(u.viewProj, vp);
    FrameUniforms::put(u.invViewProj, glm::inverse(vp));
    FrameUniforms::put4(u.camPos, glm::vec3(v.position), static_cast<float>(time));
    FrameUniforms::put4(u.sunDir, Lg.lightDir, Lg.sunIntensity);
    FrameUniforms::put4(u.sunColor, Lg.sunColor, Lg.hemiIntensity);
    FrameUniforms::put4(u.hemiSky, Lg.hemiSky, 0);
    FrameUniforms::put4(u.hemiGround, Lg.hemiGround, 0);
    FrameUniforms::put4(u.fog, Lg.fogColor, Lg.fogDensity);
    FrameUniforms::put4(u.skyTop, Lg.skyTop, Lg.sunAmount);
    FrameUniforms::put4(u.skyHorizon, Lg.skyHorizon, static_cast<float>(Lg.daylight.night));
    u.params[0] = 1.0f;
    u.params[1] = turbulent ? 1.0f : 0.0f;
    u.params[2] = Lg.envIntensity;
    // o céu desenha o disco na direção do sol, não da luz (à noite a luz vem da lua)
    FrameUniforms::put4(u.skySun, Lg.sunDir, static_cast<float>(Lg.daylight.day));
    FrameUniforms::put4(u.camDir, glm::normalize(glm::vec3(v.target - v.position)), static_cast<float>(in.width));
    // recorte pontilhado (OCC): da câmera até o alvo dela; na câmera livre, desligado
    FrameUniforms::put4(u.occCam, glm::vec3(v.position), static_cast<float>(in.height));
    FrameUniforms::put4(u.occTarget, glm::vec3(occTarget ? *occTarget : v.position), 80.0f);
    // luz de ambiente da hora (kit de dia, atmosfera no resto, nenhuma na Turbulenta)
    if (renderer.packLoaded() && env.update(Lg.sunDir, turbulent, Lg.daylight.day)) renderer.setEnvironment(env.current());
    for (std::size_t i = 0; i < 9; ++i) FrameUniforms::put4(u.sh[i], env.sh()[i], 0);
    u.params[3] = renderer.packLoaded() ? env.maxMip() : -1.0f;
    return u;
  }

  bool canAim(const proto::PrivateState& P) const {
    const auto st = static_cast<proto::PlayerState>(P.state);
    return !P.mounted && !P.cinematic && !uiOpen() && !fx.death &&
           (st == proto::PlayerState::Free || st == proto::PlayerState::Attack || st == proto::PlayerState::Skill);
  }

  void consumeCameraEvents() {
    if (fx.camRecoilPitch != 0 || fx.camRecoilYaw != 0) cam.addRecoil(fx.camRecoilPitch, fx.camRecoilYaw);
    if (fx.camShake > 0) cam.addShake(fx.camShake);
    fx.camRecoilPitch = fx.camRecoilYaw = fx.camShake = 0;
    fx.sounds.clear();  // áudio: fase 6
    if (fx.respawned || fx.turbEntered || fx.turbLeft) {
      cam.setYaw(0);
      snapCamera = true;
    }
    fx.respawned = fx.turbEntered = fx.turbLeft = false;
    if (fx.service) {
      gameUi->openPanel(panelFor(fx.service->service), fx.service->data);
      fx.service.reset();
    }
    // main.js: o painel de derrota aparece 0,7 s depois; as quatro perguntas fecham o que estiver aberto
    if (fx.death && fx.deathDelay <= 0 && !deathShown) {
      deathShown = true;
      gameUi->closeBook();
      gameUi->openPanel(Panel::Death);
    }
    if (!fx.death) deathShown = false;
    if (fx.eventDone && !eventShown) {
      eventShown = true;
      gameUi->closeBook();
      gameUi->closePanel();
      gameUi->openPanel(Panel::Event);
    }
  }

  bool uiOpen() const { return gameUi->panel() != Panel::None || gameUi->bookOpen(); }

  static Panel panelFor(proto::Service s) {
    switch (s) {
      case proto::Service::Market: return Panel::Market;
      case proto::Service::Forge: return Panel::Forge;
      case proto::Service::Trainer: return Panel::Trainer;
      case proto::Service::Storage: return Panel::Storage;
      case proto::Service::Stable: return Panel::Stable;
      case proto::Service::Board: return Panel::Board;
      case proto::Service::Canteiro: return Panel::Canteiro;
      case proto::Service::Astronomer: return Panel::Astronomer;
      case proto::Service::Loot: return Panel::Loot;
      case proto::Service::Traveler: return Panel::Traveler;
      case proto::Service::Portal: return Panel::Portal;
    }
    return Panel::None;
  }

  void handleKeys() {
    const proto::PrivateState* P = world.self();
    // observação do céu (stars.js): Esc ou F encerram
    if (P && P->cinematic) {
      if (in.hit(Key::Escape) || in.hit(Key::F)) session.send(proto::Request{proto::ReqEndObserve{}});
      return;
    }
    if (rmlUi->wantsKeyboard()) return;  // digitando no Livro
    if (in.hit(Key::Escape)) {
      if (gameUi->bookOpen()) gameUi->closeBook();
      else if (gameUi->panel() != Panel::None) gameUi->closePanel();
      else {
        gameUi->openPanel(Panel::Pause);
        paused = true;
        session.send(proto::Request{proto::ReqPause{true}});
      }
      return;
    }
    if (!P || P->state == static_cast<std::uint8_t>(proto::PlayerState::Dead)) return;
    if (in.hit(Key::Tab)) tabChord = false;
    if (in.up(Key::Tab)) {
      const bool chord = tabChord;
      tabChord = false;
      if (!chord) gameUi->toggleMenu();
    } else if (in.hit(Key::I)) {
      gameUi->toggleMenu("inventory");
    } else if (in.hit(Key::B)) {
      gameUi->toggleMenu("book");
    } else if (in.hit(Key::J)) {
      gameUi->toggleMenu("intents");
    }
    if (in.down(Key::Tab) && !uiOpen()) {
      for (int n = 1; n <= 3; ++n) {
        const Key k = n == 1 ? Key::Digit1 : n == 2 ? Key::Digit2 : Key::Digit3;
        if (in.hit(k)) {
          cam.setPreset(n);
          fx.toast(L.ui("hud.camera." + std::to_string(n)), proto::ToastKind::Info, 1.4);
          tabChord = true;
        }
      }
    }
    if (in.hit(Key::V) && !uiOpen()) {
      cam.toggleShoulder();
      fx.toast(L.ui(cam.shoulder() > 0 ? "hud.camera.right" : "hud.camera.left"), proto::ToastKind::Info, 1.6);
    }
    if (in.hit(Key::M) && !uiOpen()) gameUi->toggleMinimap();
    if (opt.dev && in.hit(Key::F12)) session.send(proto::Request{proto::ReqDev{proto::DevCommand::Night, 0, 0, 0}});
  }

  // stars.js: legenda da observação
  std::pair<std::string, std::string> observeCaption(const proto::EvObserve& o) const {
    using proto::MsgArg;
    const auto part = [&](const char* kind) {
      return o.constellation < 0 ? L.bookPart(std::string("sky.") + kind + ".isolated", {})
                                 : L.bookPart(std::string("sky.") + kind + ".constellation",
                                              {MsgArg::str(skyView.layout().constellations[static_cast<std::size_t>(o.constellation)].key)});
    };
    switch (o.caption) {
      case proto::ObserveCaption::Intact:
        return {"A Lanterna, ainda inteira", "O instrumento registra um leve desvio de brilho na Lanterna (nome provisório). Nenhum ponto sumiu — por enquanto."};
      case proto::ObserveCaption::Daylight:
        return {"Céu claro demais", "À luz do dia, o anel de bronze ainda aponta para " + part("label") + ": o instrumento marca um vazio onde havia um ponto."};
      case proto::ObserveCaption::Absence: {
        std::string w = part("where");
        if (!w.empty()) w[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(w[0])));
        return {"Uma ausência", w + " falta um ponto que os mapas antigos registram. A causa não é conhecida."};
      }
    }
    return {};
  }

  // interact.js `updateInteract`: o que a tecla F ou o clique fariam agora.
  std::string promptText(const proto::PrivateState& P, const HudContext& hc, bool uiOpen) const {
    const auto st = static_cast<proto::PlayerState>(P.state);
    if (uiOpen || P.cinematic || st == proto::PlayerState::Down || st == proto::PlayerState::Dead || st == proto::PlayerState::Channel) return {};
    const auto arg = [](const std::string& t) { return proto::MsgArgs{proto::MsgArg::str(UiSystem::escape(t))}; };
    if (hc.prompt) return L.format(L.ui("hud.prompt.use"), arg(hud.useLabel(*hc.prompt, hc)));
    if (hc.hover && hc.hover->kind == Hover::Kind::Use) return L.format(L.ui("hud.prompt.click"), arg(hud.useLabel(hc.hover->use, hc)));
    if (hc.hover && hc.hover->kind == Hover::Kind::Enemy) {
      std::string name = L.ui("hud.shadowTarget");
      if (const auto* e = world.find(hc.hover->enemy); e && data.enemies.contains(EnemyTypeId{e->type}) &&
                                                         data.enemies[EnemyTypeId{e->type}].faction != Faction::Shadow)
        name = UiSystem::lower(L.enemy(EnemyTypeId{e->type}));
      return L.format(L.ui("hud.prompt.attack"), arg(name));
    }
    if (P.mounted) return L.ui("hud.prompt.dismount");
    return {};
  }

  // ---------------------------------------------------------------- quadro de jogo
  void gameFrame(double dt, double now, std::optional<std::filesystem::path> capture) {
    session.poll(now);
    for (const proto::GameEvent& ev : session.takeEvents()) fx.apply(ev);
    consumeCameraEvents();
    fx.update(dt);
    world.update(session, now);
    if (session.state() == ClientSession::State::Rejected || session.state() == ClientSession::State::Disconnected) {
      failure = session.state() == ClientSession::State::Rejected ? L.format(L.ui("session.rejected"), {proto::MsgArg::str(session.rejectReason())})
                                                                  : L.ui("session.disconnected");
      mode = Mode::Failed;
      return;
    }
    const proto::EntityState* me = world.me();
    const proto::PrivateState* P = world.self();
    ui.clear();
    if (!me || !P) {
      gameUi->notice(L.ui("session.joining"));
      renderEmpty(capture);
      return;
    }
    if (mode != Mode::Game) gameUi->notice({});
    mode = Mode::Game;
    ++gameFrames;
    handleKeys();

    const ViewDef* view = opt.view ? findView(*opt.view) : nullptr;
    MapKind mapKind = static_cast<MapKind>(me->map);
    if (!introShown && !opt.hideHud) {
      // main.js: uma trajetória nova começa com as intenções
      introShown = true;
      if (opt.view && *opt.view == "15_inventario") gameUi->openMenu("inventory");
      else if (opt.view && *opt.view == "16_livro") gameUi->openMenu("book");
      else if (opt.view && *opt.view == "17_mapa") gameUi->openMenu("map");
      else gameUi->openPanel(Panel::Intents, 0, true);
    }
    const bool uiOpen = this->uiOpen() || P->cinematic;
    const bool aiming = !uiOpen && in.right && canAim(*P);
    platform.setRelativeMouse(aiming);

    // câmera
    CameraSubject subj;
    subj.pos = {me->x, me->y, me->z};
    subj.yaw = me->yaw;
    subj.airY = P->airY ? *P->airY : 0.0;
    subj.mounted = P->mounted;
    subj.crouchW = P->crouchW;
    subj.speedNow = me->speed;
    subj.aiming = aiming;
    if (view) {
      if (!viewTeleported && opt.dev) {
        const glm::dvec3 d = glm::normalize(glm::dvec3(view->look.x - view->pos.x, 0, view->look.z - view->pos.z));
        if (!view->turbulent)
          session.send(proto::Request{proto::ReqDev{proto::DevCommand::Teleport, view->pos.x - d.x * 8, view->pos.z - d.z * 8, 0}});
        viewTeleported = true;
      }
      if (view->turbulent) mapKind = MapKind::Turbulent;
      const StaticMap& vm = mapOf(mapKind);
      const glm::dvec3 pos(view->pos.x, std::max(view->pos.y, vm.groundHeight(view->pos.x, view->pos.z) + 2.5), view->pos.z);
      const glm::dvec3 lk(view->look.x, std::max(view->look.y, vm.groundHeight(view->look.x, view->look.z) + 1.8), view->look.z);
      cam.setFreeCam(std::make_pair(pos, lk));
    }
    if (opt.portrait && !view) {
      // de frente para o personagem, a 3,4 m, na altura do peito
      const glm::dvec3 at(me->x, me->y + 1.0, me->z), dir(std::sin(me->yaw), 0, std::cos(me->yaw));
      cam.setFreeCam(std::make_pair(at + dir * 3.4 + glm::dvec3(0, 0.5, 0), at));
    }
    const StaticMap& map = mapOf(mapKind);
    cam.view().aspect = static_cast<double>(std::max(1, in.width)) / std::max(1, in.height);
    cam.update(dt, snapCamera, subj, in, uiOpen, map);
    snapCamera = false;

    // mira e ponteiro (pointer.js)
    const ViewCamera& vc = cam.view();
    const double cx = aiming ? in.width * 0.5 : in.mouseX, cy = aiming ? in.height * 0.5 : in.mouseY;
    const Ray ray = vc.ray(cx, cy, in.width, in.height);
    const AimResult aim = computeAim(ray, subj.pos, cam.yaw(), world, data, map);
    const bool downOrDead = P->state == static_cast<std::uint8_t>(proto::PlayerState::Down) ||
                            P->state == static_cast<std::uint8_t>(proto::PlayerState::Dead);
    std::optional<Hover> hover;
    if (!uiOpen && !P->cinematic && !downOrDead && !aiming) hover = pick(world, statics, data, vc, in.mouseX, in.mouseY, in.width, in.height);

    // HUD primeiro: sabe se o clique caiu num botão
    HudContext hc;
    hc.world = &world;
    hc.fx = &fx;
    hc.L = &L;
    hc.look = &look;
    hc.data = &data;
    hc.statics = &statics;
    hc.camera = &vc;
    hc.input = &in;
    hc.clock = ClockConfig{};
    hc.prompt = nearestInRange(world, statics);
    hc.hover = hover;
    hc.aiming = aiming;
    hc.aimDist = aim.dist;
    hc.aimEnemy = aim.enemy;
    Hud::Action act = Hud::Action::None;
    if (!opt.hideHud) act = hud.draw(ui, fonts, hc);
    gameUi->notice({});
    // HUD do documento (hud.js) e o prompt de interact.js
    gameUi->showHud(!opt.hideHud);
    HudInput hi;
    hi.world = &world;
    hi.fx = &fx;
    hi.statics = &statics;
    hi.dt = dt;
    hi.now = now;
    hi.camYaw = cam.yaw();
    hi.camPitch = cam.pitch();
    hi.aiming = aiming;
    hi.aimOnTarget = aim.enemy != 0 || (hover && hover->kind == Hover::Kind::Enemy);
    hi.aimDist = aim.dist;
    hi.mouseX = aiming ? in.width * 0.5 : in.mouseX;
    hi.mouseY = aiming ? in.height * 0.5 : in.mouseY;
    hi.uiOpen = uiOpen;
    hi.prompt = promptText(*P, hc, uiOpen);
    for (const GameUi::HudAction a : gameUi->hud(hi)) {
      switch (a) {
        case GameUi::HudAction::Potion: session.send(proto::Request{proto::ReqUsePotion{}}); break;
        case GameUi::HudAction::Mount: session.send(proto::Request{proto::ReqMountAction{}}); break;
        case GameUi::HudAction::CamNorth: cam.resetNorth(); break;
        case GameUi::HudAction::MiniSize: gameUi->toggleMinimap(); break;
        case GameUi::HudAction::CaptionClose: session.send(proto::Request{proto::ReqEndObserve{}}); break;
      }
    }
    {
      PanelInput pi;
      pi.world = &world;
      pi.fx = &fx;
      pi.statics = &statics;
      pi.dt = dt;
      pi.camYaw = cam.yaw();
      pi.camSens = cam.prefs.sens == CamSensitivity::Baixa ? "baixa" : cam.prefs.sens == CamSensitivity::Alta ? "alta" : "media";
      pi.camInvert = cam.prefs.invert;
      pi.quality = quality;
      pi.muted = muted;
      PanelOutput po = gameUi->panels(pi);
      for (const proto::Request& r : po.requests) session.send(r);
      for (const std::string& l : po.local) {
        if (l == "camsens:baixa") cam.prefs.sens = CamSensitivity::Baixa;
        else if (l == "camsens:media") cam.prefs.sens = CamSensitivity::Media;
        else if (l == "camsens:alta") cam.prefs.sens = CamSensitivity::Alta;
        else if (l == "caminvert") cam.prefs.invert = !cam.prefs.invert;
        else if (l.rfind("quality:", 0) == 0) quality = l.substr(8);
        else if (l == "mute") muted = !muted;
        else if (l == "restart") fx.toast("Nova trajetória: feche o jogo e abra de novo (o progresso é salvo na fase 6).", proto::ToastKind::Info);
      }
      for (const Panel c : po.closed) {
        if (c == Panel::Pause && paused) {
          paused = false;
          session.send(proto::Request{proto::ReqPause{false}});
        }
        if (c == Panel::Event) {
          fx.eventDone.reset();
          eventShown = false;
        }
      }
    }
    // observação do céu: legenda e câmera na estrela (stars.js `observe`)
    if (fx.observe && !observing) {
      observing = true;
      const auto d = skyView.layout().dir(fx.observe->star);
      cam.setSkyDirection(glm::dvec3(d[0], d[1], d[2]));
      gameUi->caption(observeCaption(*fx.observe));
    } else if (!fx.observe && observing) {
      observing = false;
      cam.setSkyDirection(std::nullopt);
      gameUi->caption({});
    }
    const bool clickOnUi = act != Hud::Action::None || hud.blocks(in.mouseX, in.mouseY) || rmlUi->wantsMouse();
    switch (act) {
      case Hud::Action::Potion: session.send(proto::Request{proto::ReqUsePotion{}}); break;
      case Hud::Action::Mount: session.send(proto::Request{proto::ReqMountAction{}}); break;
      case Hud::Action::Respawn:
        session.send(proto::Request{proto::ReqRespawn{}});
        fx.death.reset();
        break;
      default: break;
    }

    if (aiming || uiOpen || downOrDead) {
      cam.setDragging(false);
    } else if (in.leftPressed && !clickOnUi) {
      if (hover && hover->kind == Hover::Kind::Enemy) session.send(proto::Request{proto::ReqClickAttack{hover->enemy}});
      else if (hover) session.send(proto::Request{proto::ReqClickUse{static_cast<std::uint8_t>(hover->use.kind), hover->use.id}});
      else cam.setDragging(true);
    }
    if (!in.left) cam.setDragging(false);

    // comando do passo
    CommandContext cc;
    cc.camYaw = cam.yaw();
    cc.uiOpen = uiOpen;
    cc.aim = aim;
    const proto::PlayerCommand cmd = commands.build(in, cc, tabChord);
    session.send(cmd);

    // cena
    HoverRing ring;
    if (hover && hover->kind == Hover::Kind::Enemy) {
      if (const auto* e = world.find(hover->enemy)) ring = {true, e->x, e->z, enemyRadius(*e) + 0.6, 0xff5a3c};
    } else if (hover) {
      ring = {true, hover->use.x, hover->use.z, 1.3, 0xece4d2};
    } else if (P->attackTarget) {
      if (const auto* e = world.find(P->attackTarget); e && (e->flags & proto::kFlagAlive)) ring = {true, e->x, e->z, enemyRadius(*e) + 0.6, 0xff5a3c};
    }
    const proto::WorldState* W = world.world();
    const double gameTime = W ? W->time : 0.0;
    dynamic.clear();
    unlit.clear();
    const bool packCharacters = characterViews && renderer.packLoaded();
    const SceneContext sc{&statics, &data, &look, renderer.packLoaded(), packCharacters};
    buildEntityInstances(sc, world, gameTime, dynamic);
    buildFxGeometry(fx, map, static_cast<std::uint8_t>(mapKind), ring, now, unlit);
    const double hour = opt.hour ? *opt.hour : clockAt(gameTime, ClockConfig{}).hour;
    Renderer::Frame f;
    f.uniforms = uniforms(vc, hour, mapKind == MapKind::Turbulent, now, view ? nullptr : &vc.target);
    if (W) {
      dynScene.nodeDepleted.assign(W->nodes.size(), false);
      for (std::size_t i = 0; i < W->nodes.size(); ++i) dynScene.nodeDepleted[i] = W->nodes[i].charges == 0;
      dynScene.campPalisade = W->campState == static_cast<std::uint8_t>(proto::CampPhase::Pressionado);
    }
    dynScene.shrineTaken.assign(P->turb.shrinesTaken.size(), false);
    for (std::size_t i = 0; i < P->turb.shrinesTaken.size(); ++i) dynScene.shrineTaken[i] = P->turb.shrinesTaken[i] != 0;
    dynScene.models.clear();
    if (pack && renderer.packLoaded()) collectEntityModels(*pack, world, statics, dynScene.models);
    dynScene.skinned.clear();
    dynScene.bones.clear();
    if (packCharacters) characterViews->update(world, CharacterContext{&data, &statics, glm::vec3(vc.position), dt}, dynScene);
    f.pack = packFor(vc, gameTime);
    {
      std::vector<SkyInput::Gone> gone;
      if (W)
        for (const proto::VanishedStar& vs : W->vanished) gone.push_back({vs.index, vs.time});
      f.sky = skyFor(f.uniforms, vc, hour, mapKind == MapKind::Turbulent, gameTime, std::move(gone));
    }
    f.map = static_cast<int>(mapKind);
    f.cameraPos = glm::vec3(vc.position);
    f.dynamic = &dynamic;
    f.unlit = &unlit;
    f.ui = &ui;
    f.atlas = &atlas;
    render(f, capture);
  }

  void renderEmpty(const std::optional<std::filesystem::path>& capture) {
    Renderer::Frame f;
    ViewCamera v;
    v.aspect = static_cast<double>(std::max(1, in.width)) / std::max(1, in.height);
    f.uniforms = uniforms(v, 12, false, 0);
    f.map = 0;
    f.ui = &ui;
    f.atlas = &atlas;
    render(f, capture);
  }

  // ---------------------------------------------------------------- título e criação
  void titleFrame(double dt, double now, const std::optional<std::filesystem::path>& capture) {
    titleT += dt;
    // sobre o entreposto sul, olhando para o norte (main.js TITLE_VIEW, TITLE_AZIMUTH = 350°)
    const PlaceArea& merc = statics.zones().data().mercado;
    const StaticMap& region = mapOf(MapKind::Region);
    const double vx = merc.x - 30, vz = merc.z + 60;
    const double a = titleT * 0.015;
    const glm::dvec3 pos(vx + std::sin(a) * 5, region.groundHeight(vx, vz) + 26, vz);
    const double az = (350 + std::sin(a) * 3) * 3.14159265358979 / 180, el = 0.2;
    cam.setFixed(pos, pos + glm::dvec3(std::sin(az) * 100, std::tan(el) * 100, -std::cos(az) * 100), 55);
    cam.view().aspect = static_cast<double>(std::max(1, in.width)) / std::max(1, in.height);
    ui.clear();
    GameUi::ScreenAction r = gameUi->screenInput(in, form);
    // captura da criação: abre direto (como o clique em "Nova trajetória")
    if (mode == Mode::Title && opt.view && *opt.view == "01b_create_screen") r = GameUi::ScreenAction::NewGame;
    if (mode == Mode::Title) {
      if (r == GameUi::ScreenAction::NewGame) {
        mode = Mode::Create;
        gameUi->showCreate(form);
        platform.setTextInput(true);
      }
    } else if (r == GameUi::ScreenAction::Back) {
      mode = Mode::Title;
      gameUi->showTitle(false);
      platform.setTextInput(false);
    } else if (r == GameUi::ScreenAction::Start) {
      platform.setTextInput(false);
      gameUi->hideScreens();
      join();
    }
    dynamic.clear();
    unlit.clear();
    Renderer::Frame f;
    f.uniforms = uniforms(cam.view(), opt.hour ? *opt.hour : 23.2, false, now);
    f.pack = packFor(cam.view(), 0);
    {
      // a estrela da tela de título some aos 5 s (main.js)
      std::vector<SkyInput::Gone> gone;
      if (titleT > 5) gone.push_back({skyView.layout().titleStar, 5.0});
      f.sky = skyFor(f.uniforms, cam.view(), opt.hour ? *opt.hour : 23.2, false, titleT, std::move(gone));
    }
    f.map = 0;
    f.cameraPos = glm::vec3(cam.view().position);
    f.dynamic = &dynamic;
    f.unlit = &unlit;
    f.ui = &ui;
    f.atlas = &atlas;
    render(f, capture);
  }

  // main.js boot: uma etapa por quadro, com a barra e as fases na tela antes de cada uma.
  void loadingFrame(double now) {
    struct Step {
      int phase;
      const char* sub;
    };
    static const Step steps[] = {{0, ""}, {1, ""}, {2, "relevo da região"}, {2, "Região Turbulenta"}, {2, "luz do ambiente"}, {3, ""}, {3, "mapa da região"}, {4, ""}};
    const Step& st = steps[std::min<std::size_t>(static_cast<std::size_t>(loadStep), std::size(steps) - 1)];
    if (st.phase != bootPhase) {
      bootPhase = st.phase;
      bootSubs = 0;
    }
    if (*st.sub) ++bootSubs;
    gameUi->bootProgress(st.phase, st.sub, bootSubs);
    gameUi->bootLore(now);
    ui.clear();
    renderEmpty(std::nullopt);
    switch (loadStep) {
      case 0: packStage(0); break;
      case 1: packStage(1); break;
      case 2: loadMap(MapKind::Region); break;
      case 3: loadMap(MapKind::Turbulent); break;
      case 4: packStage(2); break;
      case 5: packStage(3); break;
      case 6: gameUi->worldMap().build(statics, look); break;
      default: break;
    }
    ++loadStep;
    if (loadStep >= static_cast<int>(std::size(steps))) {
      gameUi->bootDone();
      titleT = 0;
      if (opt.autoStart) {
        gameUi->hideScreens();
        join();
      } else {
        mode = Mode::Title;
        gameUi->showTitle(false);
      }
    }
  }

  int run() {
    double last = platform.now();
    while (true) {
      const double now = platform.now();
      double dt = opt.fixedDt > 0 ? opt.fixedDt : std::min(0.05, now - last);
      last = now;
      platform.pump(in);
      if (in.quit) break;
      ++totalFrames;
      // capturas: o quadro final (N quadros depois de entrar no jogo, ou no título)
      const bool titleCapture = opt.view && (*opt.view == "01_title_screen" || *opt.view == "01b_create_screen");
      const int counted = titleCapture ? (mode == Mode::Title || mode == Mode::Create ? totalFrames : 0) : gameFrames;
      const bool last_ = opt.frames > 0 && counted + 1 >= opt.frames;
      std::optional<std::filesystem::path> capture;
      if (last_ && opt.screenshot) capture = opt.screenshot;
      switch (mode) {
        case Mode::Loading: loadingFrame(now); break;
        case Mode::Title:
        case Mode::Create: titleFrame(dt, now, capture); break;
        case Mode::Joining:
        case Mode::Game: gameFrame(dt, now, capture); break;
        case Mode::Failed:
          ui.clear();
          gameUi->notice(failure);
          renderEmpty(std::nullopt);
          if (opt.frames > 0) {
            log::error("cliente: {}", failure);
            return 1;
          }
          break;
      }
      in.endFrame();
      if (last_ && (mode == Mode::Game || titleCapture)) {
        if (capture) log::info("captura gravada em {}", pathToUtf8(*capture));
        break;
      }
      if (opt.frames > 0 && totalFrames > opt.frames + 900) {
        log::error("cliente: o jogo não começou a tempo ({} quadros)", totalFrames);
        return 1;
      }
    }
    return 0;
  }
};

ClientApp::ClientApp(const GameData& data, const StaticWorld& statics, AppOptions options, std::unique_ptr<net::ITransport> transport)
    : impl_(std::make_unique<Impl>(data, statics, std::move(options), std::move(transport))) {}

ClientApp::~ClientApp() = default;

int ClientApp::run() { return impl_->run(); }

}  // namespace rpg::client
