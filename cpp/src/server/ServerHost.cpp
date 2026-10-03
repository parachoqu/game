#include "server/ServerHost.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include "core/Log.h"
#include "core/Math.h"
#include "core/Paths.h"
#include "net/Bytes.h"
#include "server/persist/SaveStore.h"

namespace rpg::server {

namespace proto = rpg::protocol;

// Bot: um jogador sem conexão que anda ao acaso e ataca o que estiver perto (testes de carga).
struct ServerHost::Bot {
  sim::EntityId player = 0;
  Pcg32 rng;
  double changeIn = 0;
  proto::PlayerCommand cmd;
  explicit Bot(std::uint64_t seed) : rng(seed, 7) {}
};

ServerHost::ServerHost(const GameData& data, const StaticWorld& statics, ServerConfig config,
                       std::unique_ptr<net::ITransport> transport)
    : config_(config),
      transport_(std::move(transport)),
      world_(data, statics, sim::WorldConfig{config.seed, {}, config.devCommands}),
      timestep_(1.0 / config.tickRate) {
  static constexpr const char* kStarts[] = {"espadachim", "arqueiro", "lutador", "lanceiro", "mago_gelo", "piromante"};
  for (int i = 0; i < config.bots; ++i) {
    auto bot = std::make_unique<Bot>(config.seed * 1000 + static_cast<std::uint64_t>(i));
    const sim::PlayerProfile profile{"Bot " + std::to_string(i + 1), "humano", kStarts[i % 6], "kachujin"};
    bot->player = world_.addPlayer(profile, 0, true);
    bots_.push_back(std::move(bot));
  }
  if (config_.saveDir) {
    store_ = std::make_unique<persist::SaveStore>(*config_.saveDir);
    if (auto w = store_->loadWorld()) {
      persist::loadWorld(world_, *w);
      log::info("mundo salvo carregado de {} (tempo de jogo {:.0f} s)", pathToUtf8(store_->dir()), world_.time());
    }
  }
}

ServerHost::~ServerHost() = default;

void ServerHost::runTicks(std::uint64_t n) {
  for (std::uint64_t i = 0; i < n; ++i) {
    pumpNetwork();
    tickOnce();
  }
}

int ServerHost::update(Seconds elapsed) {
  pumpNetwork();
  const int steps = timestep_.advance(elapsed);
  for (int i = 0; i < steps; ++i) tickOnce();
  return steps;
}

void ServerHost::saveAll() {
  if (!store_) return;
  store_->saveWorld(persist::exportWorld(world_.state()));
  for (const Session& s : sessions_) saveCharacter(s);
}

void ServerHost::saveCharacter(const Session& s) {
  if (!store_ || !s.player) return;
  const sim::Player* p = world_.state().player(s.player);
  if (!p || !persist::canSave(*p)) return;  // dentro da Turbulenta vale o save de antes da entrada
  store_->saveCharacter(p->name, persist::exportCharacter(world_.state(), *p));
}

void ServerHost::run(const std::atomic<bool>& stop) {
  using clock = std::chrono::steady_clock;
  auto last = clock::now();
  while (!stop.load(std::memory_order_relaxed)) {
    const auto now = clock::now();
    update(std::chrono::duration<double>(now - last).count());
    last = now;
    // dorme até o próximo passo previsto; a precisão fina vem do acumulador, não do sono
    const double wait = (1.0 - timestep_.alpha()) * timestep_.step();
    std::this_thread::sleep_for(std::chrono::duration<double>(std::max(0.0005, wait)));
  }
  saveAll();
}

void ServerHost::tickOnce() {
  stepBots();
  world_.step(timestep_.step());
  routeEvents();
  if (config_.snapshotEvery <= 1 || world_.tick() % static_cast<std::uint64_t>(config_.snapshotEvery) == 0) sendSnapshots();
  if (store_ && !world_.paused()) {
    autosaveT_ += timestep_.step();
    if (autosaveT_ >= config_.autosaveEvery) {
      autosaveT_ = 0;
      saveAll();
    }
  }
}

Session* ServerHost::sessionOf(net::ConnectionId id) {
  for (Session& s : sessions_)
    if (s.connection == id) return &s;
  return nullptr;
}

void ServerHost::send(net::ConnectionId to, net::Channel ch, const proto::Bytes& bytes) {
  if (transport_) transport_->send(to, ch, net::asBytes(bytes));
}

void ServerHost::pumpNetwork() {
  if (!transport_) return;
  net::Message msg;
  while (transport_->poll(msg)) {
    switch (msg.kind) {
      case net::Message::Kind::Connected:
        sessions_.push_back({msg.peer, world_.tick(), 0, 0, {}});
        log::info("cliente {} conectado (tick {})", msg.peer.value, world_.tick());
        break;
      case net::Message::Kind::Disconnected: {
        if (Session* s = sessionOf(msg.peer); s && s->player) {
          saveCharacter(*s);
          world_.removePlayer(s->player);
        }
        std::erase_if(sessions_, [&](const Session& s) { return s.connection == msg.peer; });
        log::info("cliente {} desconectado", msg.peer.value);
        break;
      }
      case net::Message::Kind::Data: {
        Session* s = sessionOf(msg.peer);
        if (!s) {
          ++ignoredMessages_;
          break;
        }
        if (msg.channel == net::Channel::Commands) {
          auto cmd = proto::decode<proto::PlayerCommand>(net::asU8(msg.bytes));
          if (!cmd || !s->player) {
            ++ignoredMessages_;
            break;
          }
          // canal não confiável: um comando mais velho que o último aplicado chega atrasado e é descartado
          if (cmd->seq != 0 && cmd->seq <= s->lastCommandSeq) break;
          s->lastCommandSeq = cmd->seq;
          world_.setCommand(s->player, *cmd);
        } else if (msg.channel == net::Channel::Requests) {
          auto m = proto::decode<proto::ClientMessage>(net::asU8(msg.bytes));
          if (!m) {
            ++ignoredMessages_;
            break;
          }
          onClientMessage(*s, std::move(*m));
        } else {
          ++ignoredMessages_;
        }
        break;
      }
    }
  }
}

void ServerHost::join(Session& s, const proto::Hello& hello) {
  const auto reject = [&](std::string why) {
    send(s.connection, net::Channel::Events, proto::encode(proto::ServerMessage{proto::Reject{std::move(why)}}));
  };
  if (hello.protocol != proto::kProtocolVersion) return reject("versão de protocolo diferente");
  if (!config_.dataHash.empty() && !hello.dataHash.empty() && hello.dataHash != config_.dataHash)
    return reject("dados de design diferentes dos do servidor");
  std::string name = hello.name.substr(0, 32);
  if (name.empty()) name = "Viajante";
  // um personagem (um nome, um arquivo) só pode estar no mundo uma vez
  const std::string file = persist::SaveStore::fileNameFor(name);
  for (const Session& o : sessions_)
    if (&o != &s && o.player && persist::SaveStore::fileNameFor(o.name) == file) return reject("este personagem já está no mundo");

  proto::Welcome w;
  if (hello.resume && store_) {
    if (auto rec = store_->loadCharacter(name)) {
      persist::Migration m;
      if (auto migrated = persist::migrateCharacter(world_.statics(), std::move(*rec), &m)) {
        int dropped = 0;
        s.player = persist::loadCharacter(world_, *migrated, s.connection.value, &dropped);
        w.resumed = true;
        w.migration = static_cast<std::uint8_t>((m.noPosition ? proto::kMigrationNoPosition : 0) |
                                                (m.layoutChanged ? proto::kMigrationLayout : 0) |
                                                (m.invalidPosition ? proto::kMigrationInvalidPosition : 0));
        w.fromLayout = static_cast<std::uint16_t>(m.fromLayout);
        w.toLayout = static_cast<std::uint16_t>(m.toLayout);
        for (const std::string& n : m.notes) log::info("save de {}: {}", name, n);
        if (dropped) log::warn("save de {}: {} itens com chave desconhecida descartados", name, dropped);
      } else {
        log::warn("save de {} não reconhecido; começa um personagem novo", name);
      }
    }
  }
  if (!s.player) {
    const auto& data = world_.data();
    const std::string origin = data.origins.find(hello.origin) ? hello.origin : "humano";
    const std::string start = data.starts.find(hello.start) ? hello.start : "espadachim";
    s.player = world_.addPlayer({name, origin, start, hello.model}, s.connection.value, true);
  }
  s.name = world_.state().player(s.player)->name;
  s.lastCommandSeq = 0;
  // um personagem novo substitui o save de mesmo nome (main.js: clearSave ao começar)
  if (!w.resumed) saveCharacter(s);
  w.playerId = s.player;
  w.tickRate = config_.tickRate;
  w.tick = world_.tick();
  send(s.connection, net::Channel::Events, proto::encode(proto::ServerMessage{w}));
  send(s.connection, net::Channel::Events, proto::encode(proto::ServerMessage{proto::GameEvent{world_.bookSync(s.player)}}));
  log::info("{} {} (jogador {})", s.name, w.resumed ? "voltou ao mundo" : "entrou no mundo", s.player);
}

void ServerHost::leave(Session& s, bool discard) {
  if (!s.player) return;
  if (discard) {
    if (store_) store_->removeCharacter(s.name);
  } else {
    saveCharacter(s);
  }
  world_.removePlayer(s.player);
  log::info("{} saiu do mundo{}", s.name, discard ? " (save apagado)" : "");
  s.player = 0;
  s.lastCommandSeq = 0;
  s.name.clear();
}

void ServerHost::onClientMessage(Session& s, proto::ClientMessage&& msg) {
  if (auto* hello = std::get_if<proto::Hello>(&msg)) {
    if (!s.player) join(s, *hello);  // um personagem por sessão
    return;
  }
  auto& req = std::get<proto::Request>(msg);
  if (!s.player) return;
  if (const auto* lv = std::get_if<proto::ReqLeave>(&req)) return leave(s, lv->discard);
  if (std::holds_alternative<proto::ReqPause>(req) && (!config_.allowPause || sessions_.size() > 1)) return;
  world_.handleRequest(s.player, req);
}

void ServerHost::routeEvents() {
  const std::vector<sim::OutEvent> events = world_.takeEvents();
  if (!transport_ || events.empty()) return;
  const sim::Sim& S = world_.state();
  for (const Session& s : sessions_) {
    if (!s.player) continue;
    const sim::Player* p = S.player(s.player);
    if (!p) continue;
    for (const sim::OutEvent& ev : events) {
      bool deliver = false;
      switch (ev.to.type) {
        case sim::Recipient::Type::Player: deliver = ev.to.player == s.player; break;
        case sim::Recipient::Type::All: deliver = true; break;
        case sim::Recipient::Type::Instance: deliver = p->map == ev.to.map && p->instance == ev.to.instance; break;
        case sim::Recipient::Type::Near:
          deliver = p->map == ev.to.map && p->instance == ev.to.instance &&
                    jsHypot(p->pos.x - ev.to.x, p->pos.z - ev.to.z) <= ev.to.radius;
          break;
      }
      if (deliver) send(s.connection, net::Channel::Events, proto::encode(proto::ServerMessage{ev.event}));
    }
  }
}

void ServerHost::sendSnapshots() {
  if (!transport_) return;
  for (const Session& s : sessions_) {
    if (!s.player) continue;
    send(s.connection, net::Channel::Snapshots, proto::encode(world_.snapshot(s.player, config_.interestRadius)));
  }
}

void ServerHost::stepBots() {
  const sim::Sim& S = world_.state();
  const double dt = timestep_.step();
  for (auto& bp : bots_) {
    Bot& b = *bp;
    const sim::Player* p = S.player(b.player);
    if (!p) continue;
    if (p->state == sim::PlayerState::Dead) {
      world_.handleRequest(b.player, proto::ReqRespawn{});
      continue;
    }
    b.changeIn -= dt;
    if (b.changeIn <= 0) {
      b.changeIn = b.rng.range(1.5, 5);
      b.cmd.moveForward = static_cast<std::int8_t>(b.rng.rangeInt(-1, 1));
      b.cmd.moveRight = static_cast<std::int8_t>(b.rng.rangeInt(-1, 1));
      b.cmd.camYaw = b.rng.range(-kPi, kPi);
      b.cmd.held = b.rng.chance(0.3) ? proto::kHeldRun : 0;
      // ataca o inimigo mais próximo em 20 m
      const sim::Enemy* best = nullptr;
      double bd = 20;
      for (const auto& e : S.enemies) {
        if (!e->alive || e->faction == Faction::Guard || e->map != p->map || e->instance != p->instance) continue;
        const double d = jsHypot(e->pos.x - p->pos.x, e->pos.z - p->pos.z);
        if (d < bd) {
          bd = d;
          best = e.get();
        }
      }
      if (best) {
        b.cmd.moveForward = 0;
        b.cmd.moveRight = 0;
        world_.handleRequest(b.player, proto::ReqClickAttack{best->id});
      }
      if (b.rng.chance(0.2)) b.cmd.press(proto::Press::SkillQ);
      if (b.rng.chance(0.1)) b.cmd.press(proto::Press::Jump);
    }
    b.cmd.seq++;
    b.cmd.aimYaw = p->yaw;
    b.cmd.aimX = p->pos.x + js::sin(p->yaw) * 20;
    b.cmd.aimY = p->pos.y + 1.45;
    b.cmd.aimZ = p->pos.z + js::cos(p->yaw) * 20;
    world_.setCommand(b.player, b.cmd);
  }
}

}  // namespace rpg::server
