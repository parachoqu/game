// Sistemas do mundo: acampamento reativo (camp.js), evento regional (event.js), desaparecimento das
// estrelas (stars.js), Região Turbulenta (turbulent.js), NPCs e viajantes (npcs.js) e descobertas
// (discovery.js).
//
// Na demo, o que era "do jogador" nesses módulos (EVT.playerPts, SKY.observed, TS.inside) supunha um
// único jogador. Aqui a contribuição e as observações ficam no Player, e cada incursão na Turbulenta
// é uma instância própria do mapa, com santuários, saídas, sombras e colapso só dela.
#include <algorithm>
#include <cmath>

#include "core/JsMath.h"
#include "core/Math.h"
#include "core/world/RouteField.h"
#include "sim/Sim.h"

namespace rpg::sim {

using protocol::MessageId;
using protocol::MsgArg;
using protocol::MsgArgs;
using protocol::SoundId;
using protocol::ToastKind;

namespace {

void toastAll(Sim& s, MessageId m, ToastKind k, protocol::MsgArgs args = {}, std::uint16_t ms = 0,
              bool skipTurbulent = false) {
  for (auto& p : s.players) {
    if (skipTurbulent && p->turb.inside) continue;
    s.toast(*p, m, args, k, ms);
  }
}

void sfxAll(Sim& s, SoundId snd) {
  for (auto& p : s.players) s.sfx(*p, snd);
}

bool allFar(const Sim& s, double x, double z, double r) {
  for (const auto& p : s.players) {
    if (p->map != MapKind::Region || p->instance != kRegionInstance) continue;
    if (jsHypot(p->pos.x - x, p->pos.z - z) <= r) return false;
  }
  return true;
}

// ---------------------------------------------------------------- acampamento
struct CampSpawn {
  const char* type;
  double x, z;
  int patrol;  // 0 nenhuma, 1 ida, 2 volta
};

std::vector<CampSpawn> compose(const Sim& s, CampPhase state) {
  const XZ C = s.layout.campCenter, D = s.layout.campDisplaced;
  const auto& P = s.layout.campPatrol;
  switch (state) {
    case CampPhase::Estabelecido:
      return {{"garraLider", C.x + 2, C.z, 0}, {"garra", C.x - 4, C.z + 3, 0}, {"garra", C.x - 3, C.z - 4, 0},
              {"garra", C.x + 5, C.z + 4, 0},  {"garra", P[0].x, P[0].z, 1},   {"garra", P[2].x, P[2].z, 2}};
    case CampPhase::Pressionado:
      return {{"garraLider", C.x - 6, C.z, 0}, {"garra", C.x - 9, C.z + 2, 0}, {"garra", C.x - 9, C.z - 2, 0},
              {"garra", C.x - 5, C.z + 4, 0}};
    case CampPhase::Deslocado:
      return {{"garra", D.x, D.z, 0}, {"garra", D.x + 3, D.z + 2, 0}, {"garra", D.x - 2, D.z + 3, 0}};
    case CampPhase::Recuperacao: return {{"garra", C.x, C.z + 2, 0}, {"garra", C.x + 3, C.z - 2, 0}};
  }
  return {};
}

XZ campCenterOf(const Sim& s, CampPhase state) {
  return state == CampPhase::Deslocado ? s.layout.campDisplaced : s.layout.campCenter;
}

void applyComposition(Sim& s) {
  for (const EntityId id : s.camp.members)
    if (const Enemy* e = s.enemy(id); e && e->alive) removeEnemy(s, id);
  s.camp.members.clear();
  std::vector<XZ> reversed(s.layout.campPatrol.rbegin(), s.layout.campPatrol.rend());
  for (const CampSpawn& c : compose(s, s.camp.state)) {
    SpawnOpts o;
    o.leash = 38;
    if (c.patrol == 1) o.patrol = s.layout.campPatrol;
    if (c.patrol == 2) o.patrol = reversed;
    Enemy& e = spawnEnemy(s, enemyType(s, c.type), c.x, c.z, o);
    e.camp = true;
    s.camp.members.push_back(e.id);
  }
  s.camp.pending = false;
}

void setCampState(Sim& s, CampPhase next, Player* credit) {
  if (s.camp.state == next) return;
  const CampPhase prev = s.camp.state;
  s.camp.state = next;
  s.camp.since = s.time;
  s.camp.pending = true;
  if (next == CampPhase::Pressionado) {
    contribute(s, credit, "acampamento", 10);
    sfxAll(s, SoundId::Horn);
    toastAll(s, MessageId::CampPressured, ToastKind::Event);
    if (s.camp.playerKills >= 2 && credit)
      bookFirst(s, *credit, "camp-pressionado", BookCat::Feitos, BookDomain::Combate, "first.camp-pressionado");
  }
  if (next == CampPhase::Deslocado) {
    contribute(s, credit, "acampamento", 6);
    toastAll(s, MessageId::CampDisplaced, ToastKind::Event);
    if (s.camp.playerKills >= 4 && credit)
      bookFirst(s, *credit, "camp-deslocado", BookCat::Feitos, BookDomain::Combate, "first.camp-deslocado");
  }
  if (next == CampPhase::Recuperacao) toastAll(s, MessageId::CampRecovering, ToastKind::Info);
  if (next == CampPhase::Estabelecido && prev == CampPhase::Recuperacao) toastAll(s, MessageId::CampReestablished, ToastKind::Info);
  s.camp.playerKills = 0;
}

// ---------------------------------------------------------------- evento regional
constexpr const char* kContribOrder[] = {"ferramentas", "lingotes", "reconhecimento", "saqueadores", "acampamento"};

void completeEvent(Sim& s) {
  s.evt.done = true;
  s.evt.doneAt = s.time;
  s.evt.progress = 100;
  sfxAll(s, SoundId::Event);
  applyReopenEffects(s);
  for (auto& pp : s.players) {
    Player& p = *pp;
    const int pct = static_cast<int>(jsRound(eventShare(s, p) * 100));
    protocol::EvEventDone done;
    done.you = pct;
    done.others = 100 - pct;
    for (const char* k : kContribOrder) {
      const double v = p.evt.get(k);
      if (v > 0) done.parts.push_back({k, jsRound(v * 10) / 10});
    }
    if (p.evt.pts > 0) {
      const auto delivered = [&](const char* k) {
        const auto it = p.evt.delivered.find(k);
        return it == p.evt.delivered.end() ? 0 : it->second;
      };
      bookLog(s, p, BookCat::Legado, BookDomain::Comunidade, "log.reabertura",
              {MsgArg::integer(delivered("ferramentas")), MsgArg::integer(delivered("lingote")),
               MsgArg::integer(p.evt.get("reconhecimento") > 0), MsgArg::integer(p.evt.get("saqueadores") > 0),
               MsgArg::integer(p.evt.get("acampamento") > 0), MsgArg::integer(pct)},
              "passagem");
      grantTitle(s, p, "mao-passagem");
    } else {
      bookLog(s, p, BookCat::Historia, BookDomain::Comunidade, "log.reabertura-outros", {}, "passagem");
    }
    s.toPlayer(p, done);
  }
}

void addProgress(Sim& s, double pts) {
  s.evt.progress = std::min(100.0, s.evt.progress + pts);
  if (s.evt.progress >= 100 && !s.evt.done) completeEvent(s);
}

// ---------------------------------------------------------------- céu
MsgArgs starArgs(const VanishedStar& v) {
  return {MsgArg::integer(v.index), MsgArg::integer(v.constellation), MsgArg::real(v.time)};
}

// ---------------------------------------------------------------- Turbulenta
MessageId compass(double dx, double dz) {
  const double a = std::fmod(js::atan2(dx, -dz) * 180 / kPi + 360, 360);
  static constexpr MessageId dirs[] = {MessageId::DirN, MessageId::DirNE, MessageId::DirE, MessageId::DirSE,
                                       MessageId::DirS, MessageId::DirSW, MessageId::DirW, MessageId::DirNW};
  return dirs[static_cast<int>(jsRound(a / 45)) % 8];
}

void closePortal(Sim& s) {
  if (!s.turb.portal) return;
  s.turb.portal.reset();
  s.turb.next = s.time + 100 + s.rng->range(0, 40);
}

void spawnPortal(Sim& s) {
  const auto& spots = s.layout.portalSpots;
  std::vector<XZ> far;
  for (const XZ& sp : spots) {
    bool ok = true;
    for (const auto& p : s.players) {
      if (p->map != MapKind::Region || p->instance != kRegionInstance) continue;
      if (!(jsHypot(sp.x - p->pos.x, sp.z - p->pos.z) > 25)) ok = false;
    }
    if (ok) far.push_back(sp);
  }
  const std::vector<XZ>& pool = far.empty() ? spots : far;
  const XZ at = pool[s.rng->index(pool.size())];
  Portal portal;
  portal.id = s.nextPortal++;
  portal.x = at.x;
  portal.z = at.z;
  portal.expires = s.time + 150;
  portal.place = placeOf(s, MapKind::Region, at.x, at.z);
  s.turb.portal = portal;
  for (auto& pp : s.players) {
    Player& p = *pp;
    if (p.turb.inside) continue;
    s.toast(p, MessageId::PortalOpened,
            {MsgArg::message(compass(at.x - p.pos.x, at.z - p.pos.z)),
             MsgArg::ref(MsgArg::Type::Place, static_cast<std::uint16_t>(portal.place))},
            ToastKind::Turb);
    s.sfx(p, SoundId::Portal);
  }
}

Encounter& encounterFor(Sim& s, Player& p, const Enemy& e) {
  for (Encounter& enc : p.turb.encounters)
    if (enc.shadow == e.id) return enc;
  Encounter enc;
  enc.shadow = e.id;
  enc.weapon = s.data.items.find(s.data.weapons.key(s.data.enemies[e.type].weapon));
  p.turb.encounters.push_back(enc);
  return p.turb.encounters.back();
}

Encounter* findEncounter(Player& p, EntityId id) {
  for (Encounter& enc : p.turb.encounters)
    if (enc.shadow == id) return &enc;
  return nullptr;
}

std::vector<XZ> shrineWorld(const Sim& s) {
  std::vector<XZ> out;
  const XZ T = s.layout.turbulent.center;
  for (const XZ& l : s.layout.turbulent.shrines) out.push_back({T.x + l.x, T.z + l.z});
  return out;
}

MsgArgs encounterArgs(const Player& p) {
  MsgArgs a;
  for (const Encounter& e : p.turb.encounters) {
    a.push_back(MsgArg::ref(MsgArg::Type::Item, e.weapon.value));
    a.push_back(MsgArg::integer(static_cast<int>(e.result)));
  }
  return a;
}

// ---------------------------------------------------------------- NPCs
NpcRole roleOf(const std::string& r) {
  if (r == "merchant") return NpcRole::Merchant;
  if (r == "smith") return NpcRole::Smith;
  if (r == "trainer") return NpcRole::Trainer;
  if (r == "stable") return NpcRole::Stable;
  if (r == "guard") return NpcRole::Guard;
  if (r == "foreman") return NpcRole::Foreman;
  if (r == "astro") return NpcRole::Astro;
  if (r == "astronomer") return NpcRole::Astronomer;
  return NpcRole::Traveler;
}

}  // namespace

// ---------------------------------------------------------------- acampamento (camp.js)
void initCamp(Sim& s) { applyComposition(s); }

void onEnemyKilledCamp(Sim& s, Enemy& e, bool byPlayer, EntityId attacker) {
  if (!e.camp) return;
  s.camp.pop -= s.data.enemies.key(e.type) == "garraLider" ? 2 : 1;
  Player* credit = byPlayer ? s.player(attacker) : nullptr;
  if (byPlayer) {
    s.camp.playerKills++;
    if (credit) bookTally(s, *credit, "garras", BookCat::Feitos, BookDomain::Combate, "tally.garras");
  }
  if (s.camp.state == CampPhase::Estabelecido && s.camp.pop <= 5) setCampState(s, CampPhase::Pressionado, credit);
  else if ((s.camp.state == CampPhase::Pressionado || s.camp.state == CampPhase::Recuperacao) && s.camp.pop <= 2)
    setCampState(s, CampPhase::Deslocado, credit);
}

void updateCamp(Sim& s, double dt) {
  const double t = s.time - s.camp.since;
  if (s.camp.state == CampPhase::Deslocado && t > 110) {
    s.camp.pop = 2;
    setCampState(s, CampPhase::Recuperacao, nullptr);
  }
  if (s.camp.state == CampPhase::Recuperacao) {
    s.camp.regenT += dt;
    if (s.camp.regenT > 25) {
      s.camp.regenT = 0;
      s.camp.pop++;
      if (s.camp.pop >= 6) setCampState(s, CampPhase::Estabelecido, nullptr);
    }
  }
  if (s.camp.state == CampPhase::Estabelecido && s.camp.pop < 9) {
    s.camp.regenT += dt;
    if (s.camp.regenT > 40) {
      s.camp.regenT = 0;
      s.camp.pop++;
    }
  }
  if (s.camp.pending) {
    const XZ c = campCenterOf(s, s.camp.state);
    const XZ C = s.layout.campCenter;
    bool busy = false;
    for (const EntityId id : s.camp.members)
      if (const Enemy* e = s.enemy(id); e && e->alive && e->state != EnemyState::Idle) busy = true;
    if (!busy && allFar(s, c.x, c.z, 45) && allFar(s, C.x, C.z, 45)) applyComposition(s);
  }
}

// ---------------------------------------------------------------- evento regional (event.js)
void contribute(Sim& s, Player* p, const std::string& kind, double pts) {
  if (s.evt.done) return;
  if (p) {
    p->evt.contrib[kind] += pts;
    p->evt.pts += pts;
    s.evt.playersPts += pts;
  }
  addProgress(s, pts);
}

double eventShare(const Sim& s, const Player& p) {
  const double total = s.evt.playersPts + s.evt.othersPts;
  return total != 0.0 ? p.evt.pts / total : 0.0;
}

void deliver(Sim& s, Player& p, ItemId id) {
  const int n = count(p.inv, id);
  if (!n) {
    s.toast(p, MessageId::EventNoneInBag, {MsgArg::ref(MsgArg::Type::Item, id.value)}, ToastKind::Warn);
    s.sfx(p, SoundId::Deny);
    return;
  }
  const std::string& key = s.data.items.key(id);
  const bool tools = key == "ferramentas";
  const double payEach = tools ? 16 : 6, ptsEach = tools ? 8 : 3;
  removeFrom(p.inv, id, n);
  const double pay = n * (s.evt.done ? jsRound(payEach * 0.6) : payEach);
  p.coins += pay;
  s.sfx(p, SoundId::Coin);
  p.evt.delivered[key] += n;
  stat(p, "delivered_" + key, n);
  s.toast(p, MessageId::EventDelivered, {MsgArg::integer(n), MsgArg::ref(MsgArg::Type::Item, id.value), MsgArg::real(pay)}, ToastKind::Coin);
  if (!s.evt.done) contribute(s, &p, tools ? "ferramentas" : "lingotes", n * ptsEach);
}

void recon(Sim& s, Player& p) {
  if (p.evt.get("reconhecimento") > 0) return;
  contribute(s, &p, "reconhecimento", 6);
  bookFirst(s, p, "reconhecimento", BookCat::Descobertas, BookDomain::Exploracao, "first.reconhecimento");
  s.toast(p, MessageId::EventReconDone, {}, ToastKind::Event);
}

void onEnemyKilledEvent(Sim& s, Enemy& e, bool byPlayer, EntityId attacker) {
  if (!byPlayer || s.evt.done || e.faction != Faction::Bandit) return;
  Player* p = s.player(attacker);
  if (!p) return;
  // conta apenas quem cai na faixa da Passagem, medida pela própria rota curta
  if (distToPolyline(e.pos.x, e.pos.z, s.layout.shortGorge) < 40 && p->evt.get("saqueadores") < 15)
    contribute(s, p, "saqueadores", 3);
}

void updateEvent(Sim& s, double dt) {
  if (s.evt.done) return;
  s.evt.othersT += dt;
  if (s.evt.othersT > 7 && s.evt.othersPts < 55) {
    s.evt.othersT = 0;
    s.evt.othersPts += 1.5;
    addProgress(s, 1.5);
  }
}

void applyReopenEffects(Sim& s) {
  // postos ao longo da Passagem: canteiro e três pontos da rota curta
  for (const LayoutPost& g : s.layout.guardPosts) {
    SpawnOpts o;
    o.post = Enemy::Post{g.x, g.z, g.yaw};
    Enemy& e = spawnEnemy(s, enemyType(s, "guarda"), g.x, g.z, o);
    e.guardPost = true;
  }
  const ItemId tools = item(s, "ferramentas");
  s.sell[s.data.markets.find("alto").value][tools.value] = 19;
  s.sell[s.data.markets.find("vale").value][tools.value] = 14;
}

// ---------------------------------------------------------------- estrelas (stars.js)
void updateStars(Sim& s, double /*dt*/, double night) {
  if (s.time < s.skyState.next) return;
  const std::size_t n = s.skyState.vanished.size();
  if (n >= s.sky.order.size()) {
    s.skyState.next = std::numeric_limits<double>::infinity();
    return;
  }
  const VanishStep& o = s.sky.order[n];
  s.skyState.vanished.push_back({o.index, o.constellation, s.time});
  s.skyState.next = s.time + 85 + s.rng->next() * 40;
  for (auto& pp : s.players) {
    Player& p = *pp;
    s.toPlayer(p, protocol::EvStarVanished{o.index});
    if (night > 0.5 && p.zone != ZoneKind::Turbulenta) {
      s.sfx(p, SoundId::Star);
      s.toast(p, MessageId::StarVanished, {}, ToastKind::Sky);
    }
  }
}

void observe(Sim& s, Player& p, double night) {
  const VanishedStar* last = s.skyState.vanished.empty() ? nullptr : &s.skyState.vanished.back();
  protocol::EvObserve ev;
  ev.star = last ? last->index : s.sky.order[1].index;
  ev.constellation = last ? last->constellation : 0;
  ev.gap = last != nullptr;
  if (!last) {
    ev.caption = protocol::ObserveCaption::Intact;
    bookFirst(s, p, "obs-inicial", BookCat::Descobertas, BookDomain::Ceu, "first.obs-inicial");
  } else if (night < 0.45) {
    ev.caption = protocol::ObserveCaption::Daylight;
    if (!p.observed.contains(last->index)) {
      p.observed.insert(last->index);
      bookLog(s, p, BookCat::Descobertas, BookDomain::Ceu, "log.obs-dia", starArgs(*last), "obs" + std::to_string(last->index));
    }
  } else {
    ev.caption = protocol::ObserveCaption::Absence;
    if (!p.observed.contains(last->index)) {
      p.observed.insert(last->index);
      bookLog(s, p, BookCat::Descobertas, BookDomain::Ceu, "log.obs-noite", starArgs(*last), "obs" + std::to_string(last->index));
    }
    grantTitle(s, p, "olhar-observatorio");
  }
  p.cinematic = true;
  s.toPlayer(p, ev);
}

void endObserve(Sim& s, Player& p) {
  if (!p.cinematic) return;
  p.cinematic = false;
  s.toPlayer(p, protocol::EvObserveEnd{});
}

void giveInstrument(Sim& s, Player& p) {
  s.skyState.instrumentGiven = true;
  p.coins += 60;
  bookLog(s, p, BookCat::Legado, BookDomain::Ceu, "log.instrumento", {}, "instrumento");
  s.toast(p, MessageId::AstronomerPaid, {}, ToastKind::Coin);
  s.sfx(p, SoundId::Event);
}

// ---------------------------------------------------------------- Turbulenta (turbulent.js)
std::optional<MessageId> canEnter(const Player& p) {
  if (p.mounted) return MessageId::MountNoTurbulent;
  if (p.state != PlayerState::Free) return MessageId::RequestTooFar;
  return std::nullopt;
}

void forcePortalNear(Sim& s, Player& p) {
  closePortal(s);
  const double x = p.pos.x + js::sin(p.yaw) * 8, z = p.pos.z + js::cos(p.yaw) * 8;
  Portal portal;
  portal.id = s.nextPortal++;
  portal.x = x;
  portal.z = z;
  portal.expires = s.time + 150;
  portal.place = placeOf(s, MapKind::Region, x, z);
  s.turb.portal = portal;
}

void enterTurbulent(Sim& s, Player& p) {
  const Portal portal = *s.turb.portal;
  const auto& L = s.layout.turbulent;
  const XZ T = L.center;
  Incursion& ts = p.turb;
  ts = Incursion{};
  ts.origin = {portal.x + 2.5, portal.z + 2.5};
  ts.inside = true;
  ts.instance = s.nextInstance++;
  ts.enteredAt = s.time;
  ts.collapseAt = s.time + s.data.balance.turbulent.duration;
  ts.fragIn = count(p.inv, item(s, "fragmento"));
  for (const XZ& l : L.shrines) ts.shrines.push_back({T.x + l.x, T.z + l.z, false});
  for (const XZ& l : L.exits) ts.exits.push_back({T.x + l.x, T.z + l.z});

  const XZ start = L.starts[s.rng->index(L.starts.size())];
  const double sx = start.x + s.rng->range(-6, 6), sz = start.z + s.rng->range(-6, 6);
  p.map = MapKind::Turbulent;
  p.instance = ts.instance;
  p.pos = {T.x + sx, s.mapOf(MapKind::Turbulent).groundHeight(T.x + sx, T.z + sz), T.z + sz};
  p.yaw = js::atan2(L.portalCore.x - sx, L.portalCore.z - sz);
  // figuras desconhecidas: outros "jogadores" simulados, identidades ocultas
  const char* types[] = {"sombraEspada", "sombraArco", "sombraMagica"};
  const std::vector<XZ> patrol = shrineWorld(s);
  for (int i = 0; i < 3; ++i) {
    const XZ l = L.shrines[static_cast<std::size_t>(i) % L.shrines.size()];
    const double x = T.x + l.x + s.rng->range(-6, 6);
    const double z = T.z + l.z + s.rng->range(-6, 6);
    SpawnOpts o;
    o.leash = 70;
    o.patrol = patrol;
    o.map = MapKind::Turbulent;
    o.instance = ts.instance;
    Enemy& e = spawnEnemy(s, enemyType(s, types[i]), x, z, o);
    e.pi = i;
    ts.shadows.push_back(e.id);
  }
  closePortal(s);
  stat(p, "incursoes");
  s.sfx(p, SoundId::Portal);
  p.anon = true;
  s.toPlayer(p, protocol::EvTurbEnter{});
  s.toast(p, MessageId::TurbEntered, {}, ToastKind::Turb, 6000);
}

void leaveTurbulent(Sim& s, Player& p, bool success) {
  Incursion& ts = p.turb;
  const int frags = count(p.inv, item(s, "fragmento")) - ts.fragIn;
  const MsgArgs enc = encounterArgs(p);
  for (const EntityId id : ts.shadows) removeEnemy(s, id);
  ts.shadows.clear();
  ts.inside = false;
  p.anon = false;
  if (success) {
    p.map = MapKind::Region;
    p.instance = kRegionInstance;
    p.pos = {ts.origin.x, s.mapOf(MapKind::Region).groundHeight(ts.origin.x, ts.origin.z), ts.origin.z};
    stat(p, "extracoes");
    MsgArgs args{MsgArg::integer(std::max(0, frags))};
    args.insert(args.end(), enc.begin(), enc.end());
    bookLog(s, p, BookCat::Historia, BookDomain::Risco, "log.incursao", args, "turb" + std::to_string(p.book.seq()));
    grantTitle(s, p, "voltou-turbulenta");
    s.toast(p, MessageId::TurbExtracted, {}, ToastKind::Event);
  } else {
    bookLog(s, p, BookCat::Historia, BookDomain::Risco, "log.incursao-perdida", enc, "turb" + std::to_string(p.book.seq()));
  }
  s.toPlayer(p, protocol::EvTurbLeave{success});
}

void onTurbulentDeath(Sim& s, Player& p, const DamageSource& src) {
  for (const EntityId id : p.turb.shadows) {
    Encounter* enc = findEncounter(p, id);
    const Enemy* e = s.enemy(id);
    if (enc && enc->result == Encounter::Result::None && e && e->alive)
      enc->result = src.faction == Faction::Shadow ? Encounter::Result::StoodUp : Encounter::Result::NoOutcome;
  }
  leaveTurbulent(s, p, false);
}

void updateTurbulent(Sim& s, double /*dt*/) {
  if (s.turb.portal) {
    if (s.time > s.turb.portal->expires) {
      closePortal(s);
      toastAll(s, MessageId::PortalClosed, ToastKind::Turb, {}, 0, true);
    }
  } else {
    bool anyInside = false;
    for (const auto& p : s.players) anyInside = anyInside || p->turb.inside;
    if (!anyInside && s.time > s.turb.next && s.running && s.opts.portals && !s.players.empty()) spawnPortal(s);
  }

  for (auto& pp : s.players) {
    Player& p = *pp;
    Incursion& ts = p.turb;
    if (!ts.inside) continue;
    // registrar encontros (anônimos) e seus desfechos
    for (const EntityId id : ts.shadows) {
      const Enemy* e = s.enemy(id);
      if (!e) continue;
      if (e->state == EnemyState::Chase || e->state == EnemyState::Windup) encounterFor(s, p, *e);
      if (Encounter* enc = findEncounter(p, id); !e->alive && enc && enc->result == Encounter::Result::None)
        enc->result = Encounter::Result::Fell;
    }
    if (!ts.lateSpawn && s.time - ts.enteredAt > 100) {
      ts.lateSpawn = true;
      const auto& L = s.layout.turbulent;
      const XZ l = L.shrines[s.rng->index(L.shrines.size())];
      SpawnOpts o;
      o.leash = 70;
      o.patrol = shrineWorld(s);
      o.map = MapKind::Turbulent;
      o.instance = ts.instance;
      Enemy& e = spawnEnemy(s, enemyType(s, "sombraArco"), L.center.x + l.x, L.center.z + l.z, o);
      ts.shadows.push_back(e.id);
      s.toast(p, MessageId::TurbLateShadow, {}, ToastKind::Turb);
    }
    const double left = ts.collapseAt - s.time;
    for (const int w : {60, 20}) {
      if (left < w && !ts.warned.contains(w)) {
        ts.warned.insert(w);
        s.sfx(p, SoundId::Horn);
        s.toast(p, MessageId::TurbCollapseWarning, {MsgArg::integer(w)}, ToastKind::Danger);
      }
    }
    // A demo testa só `state !== 'dead'` e repõe stateT = 4,4 a cada passo: como updatePlayer soma dt
    // antes, o personagem ficava derrubado para sempre e a derrota nunca acontecia. Aqui o colapso
    // derruba uma vez e a contagem segue até a derrota (ver ARQUITETURA.md, desvios da demo).
    if (left <= 0 && p.state != PlayerState::Dead && p.state != PlayerState::Down) {
      for (const EntityId id : ts.shadows)
        if (Encounter* enc = findEncounter(p, id); enc && enc->result == Encounter::Result::None)
          enc->result = Encounter::Result::NoOutcome;
      p.hp = 0;
      p.state = PlayerState::Down;
      p.stateT = 4.4;
      DamageSource src;
      src.hasPos = false;
      src.cause = MessageId::CauseCollapse;
      p.downSrc = src;
    }
  }
}

// ---------------------------------------------------------------- NPCs (npcs.js)
void initNpcs(Sim& s) {
  // os NPCs fixos na ordem da demo, depois os viajantes (initTravelers sorteia a espera de cada um)
  for (std::size_t i = 0; i < s.layout.npcs.size(); ++i) {
    const LayoutNpc& d = s.layout.npcs[i];
    if (d.traveler >= 0 && !s.opts.travelers) continue;
    Npc n;
    n.id = s.newId();
    n.layoutIndex = static_cast<std::uint32_t>(i);
    n.role = d.traveler >= 0 ? NpcRole::Traveler : roleOf(d.role);
    n.face = d.face;
    n.work = d.work;
    n.pos = {d.x, s.mapOf(MapKind::Region).groundHeight(d.x, d.z), d.z};
    n.yaw = d.yaw;
    n.traveler = d.traveler;
    if (d.traveler >= 0) {
      n.seg = 0;
      n.dir = 1;
      n.wait = s.rng->next() * 3;
    }
    s.npcs.push_back(n);
  }
}

void updateNpcs(Sim& s, double dt) {
  for (Npc& n : s.npcs) {
    double dP = 0;
    Player* P = s.nearestPlayer(MapKind::Region, kRegionInstance, n.pos.x, n.pos.z, &dP);
    const bool near = P && dP < 70;
    double v = 0;
    if (n.role == NpcRole::Traveler) {
      const auto& path = s.layout.travelers[static_cast<std::size_t>(n.traveler)].path;
      if (n.wait > 0) {
        n.wait -= dt;
      } else if (P && dP < 2.6) {
        n.yaw = js::atan2(P->pos.x - n.pos.x, P->pos.z - n.pos.z);
      } else {
        const int nextI = n.seg + n.dir;
        if (nextI < 0 || nextI >= static_cast<int>(path.size())) {
          n.dir *= -1;
          n.wait = 4;
        } else {
          const XZ t = path[static_cast<std::size_t>(nextI)];
          const double dx = t.x - n.pos.x, dz = t.z - n.pos.z, d = jsHypot(dx, dz);
          if (d < 0.6) {
            n.seg = nextI;
          } else {
            const double st = std::min(d, 3.4 * dt);
            n.pos.x += dx / d * st;
            n.pos.z += dz / d * st;
            n.yaw = js::atan2(dx, dz);
            v = 3.4;
          }
        }
      }
      n.pos.y = s.mapOf(MapKind::Region).groundHeight(n.pos.x, n.pos.z);
    } else if (n.face && near) {
      if (dP < 7) {
        const double to = js::atan2(P->pos.x - n.pos.x, P->pos.z - n.pos.z);
        n.yaw += js::atan2(js::sin(to - n.yaw), js::cos(to - n.yaw)) * std::min(1.0, dt * 4);
      }
    }
    n.speedNow = v;
  }
}

// ---------------------------------------------------------------- descobertas (discovery.js)
void updateDiscovery(Sim& s, Player& p, double dt) {
  p.discoveryT -= dt;
  if (p.discoveryT > 0) return;
  p.discoveryT = 0.5;
  for (const LayoutDiscovery& d : s.layout.discoveries) {
    const std::string flag = "disc_" + d.key;
    if (p.flags.contains(flag)) continue;
    const bool inside = d.byZone ? p.zone == ZoneKind::FullLoot : jsHypot(p.pos.x - d.x, p.pos.z - d.z) < d.r;
    if (inside) {
      p.flags.insert(flag);
      bookFirst(s, p, "disc-" + d.key, BookCat::Descobertas, BookDomain::Exploracao, "first.disc-" + d.key);
    }
  }
}

}  // namespace rpg::sim
