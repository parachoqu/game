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
#include "client/ui/Screens.h"
#include "client/ui/UiBatch.h"
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

bool ClientApp::knownView(const std::string& name) { return name == "01_title_screen" || findView(name) != nullptr; }

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
  Screens screens;
  CreationForm form;
  InputState in;
  Mode mode = Mode::Loading;
  int loadStep = 0;
  double titleT = 0, lastCmdSent = -1;
  bool tabChord = false, paused = false, snapCamera = true, viewTeleported = false;
  int gameFrames = 0, totalFrames = 0;
  std::optional<proto::Service> openService;
  std::string failure;
  InstanceLists dynamic;
  std::vector<ColorVertex> unlit;
  std::array<bool, 2> mapUploaded{};

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
    form.name = kSuggestedNames[rd() % kSuggestedNames.size()];
    in.width = opt.width;
    in.height = opt.height;
    log::info("GPU: {}", renderer.driver());
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
  FrameUniforms uniforms(const ViewCamera& v, double hour, bool turbulent, double time) const {
    const SceneLighting Lg = lightingAt(hour, turbulent);
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
    if (!turbulent) FrameUniforms::put4(u.sunDir, Lg.lightDir, Lg.sunIntensity);
    return u;
  }

  bool canAim(const proto::PrivateState& P) const {
    const auto st = static_cast<proto::PlayerState>(P.state);
    return !P.mounted && !P.cinematic && !openService && !fx.death &&
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
      openService = fx.service->service;
      fx.service.reset();
    }
  }

  void handleKeys() {
    const proto::PrivateState* P = world.self();
    if (in.hit(Key::Escape)) {
      if (openService) {
        openService.reset();
      } else if (!fx.death) {
        paused = !paused;
        session.send(proto::Request{proto::ReqPause{paused}});
      }
    }
    if (!P || P->state == static_cast<std::uint8_t>(proto::PlayerState::Dead)) return;
    if (in.hit(Key::Tab)) tabChord = false;
    if (in.down(Key::Tab) && !openService) {
      for (int n = 1; n <= 3; ++n) {
        const Key k = n == 1 ? Key::Digit1 : n == 2 ? Key::Digit2 : Key::Digit3;
        if (in.hit(k)) {
          cam.setPreset(n);
          fx.toast(L.ui("hud.camera." + std::to_string(n)), proto::ToastKind::Info, 1.4);
          tabChord = true;
        }
      }
    }
    if (in.up(Key::Tab)) tabChord = false;
    if (in.hit(Key::V) && !openService) {
      cam.toggleShoulder();
      fx.toast(L.ui(cam.shoulder() > 0 ? "hud.camera.right" : "hud.camera.left"), proto::ToastKind::Info, 1.6);
    }
    if (opt.dev && in.hit(Key::F12)) session.send(proto::Request{proto::ReqDev{proto::DevCommand::Night, 0, 0, 0}});
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
      screens.notice(ui, fonts, in, L.ui("session.joining"));
      renderEmpty(capture);
      return;
    }
    mode = Mode::Game;
    ++gameFrames;
    handleKeys();

    const ViewDef* view = opt.view ? findView(*opt.view) : nullptr;
    MapKind mapKind = static_cast<MapKind>(me->map);
    const bool uiOpen = openService.has_value() || (fx.death && fx.deathDelay <= 0) || paused;
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
    if (!opt.hideHud) {
      act = hud.draw(ui, fonts, hc);
      if (openService) drawServicePlaceholder();
      if (paused) screens.notice(ui, fonts, in, "Pausa — Esc volta ao jogo");
    }
    const bool clickOnUi = act != Hud::Action::None || hud.blocks(in.mouseX, in.mouseY);
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
    const SceneContext sc{&statics, &data, &look};
    buildEntityInstances(sc, world, gameTime, dynamic);
    buildFxGeometry(fx, map, static_cast<std::uint8_t>(mapKind), ring, now, unlit);
    const double hour = opt.hour ? *opt.hour : clockAt(gameTime, ClockConfig{}).hour;
    Renderer::Frame f;
    f.uniforms = uniforms(vc, hour, mapKind == MapKind::Turbulent, now);
    f.map = static_cast<int>(mapKind);
    f.cameraPos = glm::vec3(vc.position);
    f.dynamic = &dynamic;
    f.unlit = &unlit;
    f.ui = &ui;
    f.atlas = &atlas;
    renderer.render(f, capture);
  }

  void drawServicePlaceholder() {
    const float sw = static_cast<float>(in.width), sh = static_cast<float>(in.height);
    const float w = std::min(520.0f, sw - 40), h = 120;
    drawPanel(ui, sw / 2 - w / 2, sh / 2 - h / 2, w, h);
    static const char* names[] = {"Mercado", "Bancada", "Instrutor", "Armazém", "Estábulo", "Quadro de relatos", "Canteiro",
                                  "Astrônoma", "Carga no chão", "Viajante", "Rasgo violeta"};
    const auto i = static_cast<std::size_t>(*openService);
    ui.text(fonts.display, 22, sw / 2, sh / 2 - 40, i < std::size(names) ? names[i] : "Painel", Rgba::hex(0xece4d2), Align::Center);
    ui.text(fonts.body, 13, sw / 2, sh / 2 + 4, "Esc fecha", Rgba::hex(0xa8a193), Align::Center);
  }

  void renderEmpty(const std::optional<std::filesystem::path>& capture) {
    Renderer::Frame f;
    ViewCamera v;
    v.aspect = static_cast<double>(std::max(1, in.width)) / std::max(1, in.height);
    f.uniforms = uniforms(v, 12, false, 0);
    f.map = 0;
    f.ui = &ui;
    f.atlas = &atlas;
    renderer.render(f, capture);
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
    if (mode == Mode::Title) {
      const Screens::Result r = screens.title(ui, fonts, in, L, false, std::min(1.0, titleT / 0.6));
      if (r == Screens::Result::NewGame) {
        mode = Mode::Create;
        platform.setTextInput(true);
      }
    } else {
      const Screens::Result r = screens.create(ui, fonts, in, L, data, look, form);
      if (r == Screens::Result::Back) {
        mode = Mode::Title;
        platform.setTextInput(false);
      } else if (r == Screens::Result::Start) {
        platform.setTextInput(false);
        join();
      }
    }
    dynamic.clear();
    unlit.clear();
    Renderer::Frame f;
    f.uniforms = uniforms(cam.view(), opt.hour ? *opt.hour : 23.2, false, now);
    f.map = 0;
    f.cameraPos = glm::vec3(cam.view().position);
    f.dynamic = &dynamic;
    f.unlit = &unlit;
    f.ui = &ui;
    f.atlas = &atlas;
    renderer.render(f, capture);
  }

  void loadingFrame() {
    static const char* steps[] = {"relevo da região", "relevo da Turbulenta", "pronto"};
    ui.clear();
    screens.loading(ui, fonts, in, L, loadStep / 2.0, steps[std::min(loadStep, 2)]);
    renderEmpty(std::nullopt);
    if (loadStep == 0) loadMap(MapKind::Region);
    else if (loadStep == 1) loadMap(MapKind::Turbulent);
    ++loadStep;
    if (loadStep >= 2) {
      titleT = 0;
      if (opt.autoStart) join();
      else mode = Mode::Title;
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
      const bool titleCapture = opt.view && *opt.view == "01_title_screen";
      const int counted = titleCapture ? (mode == Mode::Title ? totalFrames : 0) : gameFrames;
      const bool last_ = opt.frames > 0 && counted + 1 >= opt.frames;
      std::optional<std::filesystem::path> capture;
      if (last_ && opt.screenshot) capture = opt.screenshot;
      switch (mode) {
        case Mode::Loading: loadingFrame(); break;
        case Mode::Title:
        case Mode::Create: titleFrame(dt, now, capture); break;
        case Mode::Joining:
        case Mode::Game: gameFrame(dt, now, capture); break;
        case Mode::Failed:
          ui.clear();
          screens.notice(ui, fonts, in, failure);
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
