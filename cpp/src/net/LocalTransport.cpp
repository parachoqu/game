#include "net/LocalTransport.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <utility>

#include "core/Assert.h"
#include "core/Rng.h"

namespace rpg::net {

double steadySeconds() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

namespace {

struct Pending {
  double deliverAt;
  std::uint64_t seq;  // desempate estável: mesma hora de entrega → ordem de envio
  Message msg;
};

struct Inbox {
  std::deque<Pending> queue;  // ordenada por (deliverAt, seq)
  // Última entrega agendada por (remetente, canal confiável): nada confiável passa à frente dela.
  std::map<std::pair<std::uint32_t, Channel>, double> reliableTail;
};

}  // namespace

struct LocalHub::State {
  std::mutex mutex;
  NetSim sim;
  TimeSource now;
  Pcg32 rng;
  std::uint64_t seq = 0;
  std::uint32_t nextClient = 1;
  std::map<std::uint32_t, Inbox> inboxes;  // 0 = servidor

  State(NetSim s, TimeSource n) : sim(s), now(std::move(n)), rng(s.seed) {}

  // Chamar com o mutex travado.
  void deliver(std::uint32_t from, std::uint32_t to, Message msg) {
    const auto it = inboxes.find(to);
    if (it == inboxes.end()) return;  // destino já desconectou
    Inbox& box = it->second;
    const bool control = msg.kind != Message::Kind::Data;
    const bool reliable = control || isReliable(msg.channel);
    if (!reliable && sim.lossPercent > 0.0 && rng.chance(sim.lossPercent / 100.0)) return;
    double at = now() + sim.latencyMs / 1000.0;
    if (sim.jitterMs > 0.0) at += rng.next() * sim.jitterMs / 1000.0;
    if (reliable) {
      double& tail = box.reliableTail[{from, control ? Channel::Count : msg.channel}];
      at = std::max(at, tail);
      tail = at;
    }
    Pending p{at, seq++, std::move(msg)};
    const auto pos = std::upper_bound(box.queue.begin(), box.queue.end(), p, [](const Pending& a, const Pending& b) {
      return a.deliverAt != b.deliverAt ? a.deliverAt < b.deliverAt : a.seq < b.seq;
    });
    box.queue.insert(pos, std::move(p));
  }

  bool take(std::uint32_t self, Message& out) {
    const auto it = inboxes.find(self);
    if (it == inboxes.end() || it->second.queue.empty()) return false;
    Pending& front = it->second.queue.front();
    if (front.deliverAt > now()) return false;
    out = std::move(front.msg);
    it->second.queue.pop_front();
    return true;
  }
};

namespace {

class Endpoint final : public ITransport {
 public:
  Endpoint(std::shared_ptr<LocalHub::State> state, std::uint32_t self) : state_(std::move(state)), self_(self) {}

  ~Endpoint() override {
    const std::lock_guard lock(state_->mutex);
    Message bye;
    bye.kind = Message::Kind::Disconnected;
    if (self_ == kServer.value) {
      for (auto& [id, box] : state_->inboxes) {
        if (id == self_) continue;
        bye.peer = kServer;
        state_->deliver(self_, id, bye);
      }
    } else {
      bye.peer = ConnectionId{self_};
      state_->deliver(self_, kServer.value, std::move(bye));
    }
    state_->inboxes.erase(self_);
  }

  void send(ConnectionId to, Channel channel, std::span<const std::byte> bytes) override {
    RPG_ASSERT(channel != Channel::Count, "canal inválido");
    const std::lock_guard lock(state_->mutex);
    Message msg;
    msg.kind = Message::Kind::Data;
    msg.peer = self_ == kServer.value ? kServer : ConnectionId{self_};
    msg.channel = channel;
    msg.bytes.assign(bytes.begin(), bytes.end());
    const std::uint32_t target = self_ == kServer.value ? to.value : kServer.value;
    state_->deliver(self_, target, std::move(msg));
  }

  bool poll(Message& out) override {
    const std::lock_guard lock(state_->mutex);
    return state_->take(self_, out);
  }

 private:
  std::shared_ptr<LocalHub::State> state_;
  std::uint32_t self_;
};

}  // namespace

LocalHub::LocalHub(NetSim sim, TimeSource now) : state_(std::make_shared<State>(sim, std::move(now))) {}

LocalHub::~LocalHub() = default;

std::unique_ptr<ITransport> LocalHub::serverEndpoint() {
  RPG_ASSERT(!serverTaken_, "LocalHub: a ponta do servidor já foi entregue");
  serverTaken_ = true;
  const std::lock_guard lock(state_->mutex);
  state_->inboxes[kServer.value];
  return std::make_unique<Endpoint>(state_, kServer.value);
}

std::unique_ptr<ITransport> LocalHub::connectClient() {
  const std::lock_guard lock(state_->mutex);
  const std::uint32_t id = state_->nextClient++;
  state_->inboxes[id];
  Message hello;
  hello.kind = Message::Kind::Connected;
  hello.peer = ConnectionId{id};
  state_->deliver(id, kServer.value, std::move(hello));
  return std::make_unique<Endpoint>(state_, id);
}

}  // namespace rpg::net
