#include "client/fx/FxState.h"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <variant>

#include "client/ui/Localization.h"

namespace rpg::client {

namespace proto = protocol;

void FxState::toast(std::string text, proto::ToastKind kind, double seconds) {
  toasts.push_front({std::move(text), kind, seconds, seconds, ++toastSeq_});
  while (toasts.size() > kMaxToasts) toasts.pop_back();
}

void FxState::clearWorld() {
  telegraphs.clear();
  particles.clear();
  floats.clear();
}

void FxState::apply(const proto::GameEvent& event) {
  std::visit(
      [&](const auto& ev) {
        using T = std::decay_t<decltype(ev)>;
        if constexpr (std::is_same_v<T, proto::EvSound>) {
          sounds.push_back(ev.sound);
        } else if constexpr (std::is_same_v<T, proto::EvToast>) {
          toast(L_->message(ev.msg, ev.args), ev.kind, ev.durationMs ? ev.durationMs / 1000.0 : 4.2);
        } else if constexpr (std::is_same_v<T, proto::EvFloatText>) {
          floats.push_back({{ev.x, ev.y, ev.z}, L_->message(ev.msg, ev.args), ev.kind, 0});
        } else if constexpr (std::is_same_v<T, proto::EvBurst>) {
          std::uniform_real_distribution<double> u(0.0, 1.0);
          for (int i = 0; i < ev.count; ++i) {
            const double a = u(rng_) * 6.283185307179586, r = u(rng_);
            particles.push_back({{ev.x, ev.y, ev.z},
                                 {std::cos(a) * ev.speed * r, 2 + u(rng_) * ev.speed, std::sin(a) * ev.speed * r},
                                 ev.color, 0.5 + u(rng_) * 0.4});
          }
        } else if constexpr (std::is_same_v<T, proto::EvTelegraph>) {
          telegraphs.push_back({ev, 0, false});
        } else if constexpr (std::is_same_v<T, proto::EvTelegraphCancel>) {
          for (TelegraphFx& t : telegraphs)
            if (t.ev.id == ev.id) t.cancelled = true;
        } else if constexpr (std::is_same_v<T, proto::EvHitmarker>) {
          hitmarker = 0.13;
          hitmarkerCrit = ev.crit;
          sounds.push_back(proto::SoundId::Hitmark);
        } else if constexpr (std::is_same_v<T, proto::EvCamRecoil>) {
          std::uniform_real_distribution<double> u(-1.0, 1.0);
          camRecoilPitch += ev.pitch;
          camRecoilYaw += ev.yawJitter * u(rng_);
        } else if constexpr (std::is_same_v<T, proto::EvCamShake>) {
          camShake += ev.amount;
        } else if constexpr (std::is_same_v<T, proto::EvZoneChanged>) {
          zoneChangedTo = ev.to;
          zoneExpand = 7;
        } else if constexpr (std::is_same_v<T, proto::EvPlaceChanged>) {
          // o HUD lê o lugar do estado privado
        } else if constexpr (std::is_same_v<T, proto::EvPlayerHurt>) {
          hurtFlash = 0.09;
        } else if constexpr (std::is_same_v<T, proto::EvPlayerDown>) {
        } else if constexpr (std::is_same_v<T, proto::EvPlayerDied>) {
          death = ev.report;
          deathDelay = 0.7;
          service.reset();
        } else if constexpr (std::is_same_v<T, proto::EvRespawned>) {
          death.reset();
          respawned = true;
          clearWorld();
        } else if constexpr (std::is_same_v<T, proto::EvServiceOpened>) {
          service = ev;
        } else if constexpr (std::is_same_v<T, proto::EvEventDone>) {
          service.reset();
          eventDone = ev;
        } else if constexpr (std::is_same_v<T, proto::EvObserve>) {
          observe = ev;
        } else if constexpr (std::is_same_v<T, proto::EvObserveEnd>) {
          observe.reset();
        } else if constexpr (std::is_same_v<T, proto::EvStarVanished>) {
        } else if constexpr (std::is_same_v<T, proto::EvTurbEnter>) {
          turbEntered = true;
          clearWorld();
        } else if constexpr (std::is_same_v<T, proto::EvTurbLeave>) {
          turbLeft = true;
          clearWorld();
        } else if constexpr (std::is_same_v<T, proto::EvBookEntry>) {
          auto it = std::find_if(book.begin(), book.end(), [&](const proto::BookEntry& b) { return b.id == ev.entry.id; });
          if (it == book.end()) book.push_back(ev.entry);
          else *it = ev.entry;
          // hud.js: "O Livro registrou" só na primeira vez e nunca para anotações pessoais
          if (ev.entry.count == 1 && !ev.entry.personal)
            toast(L_->ui("book.registered") + " " + L_->bookTitle(ev.entry), proto::ToastKind::Book, 5.2);
        } else if constexpr (std::is_same_v<T, proto::EvBookTitle>) {
          titles.push_back({ev.title, {}, {}});
          toast(L_->ui("book.newTitle") + " " + L_->bookTitleName({ev.title, {}, {}}), proto::ToastKind::Book, 6.0);
        } else if constexpr (std::is_same_v<T, proto::EvLootView>) {
          loot = ev;
        } else if constexpr (std::is_same_v<T, proto::EvBookSync>) {
          book = ev.entries;
          titles = ev.titles;
          shownTitle = ev.shownTitle;
        }
      },
      event);
}

void FxState::update(double dt) {
  for (auto it = telegraphs.begin(); it != telegraphs.end();) {
    it->t += dt;
    if (it->cancelled || it->t > it->ev.duration + 0.12) it = telegraphs.erase(it);
    else ++it;
  }
  for (auto it = particles.begin(); it != particles.end();) {
    it->life -= dt;
    it->vel.y -= 14 * dt;
    it->pos += it->vel * dt;
    if (it->life <= 0) it = particles.erase(it);
    else ++it;
  }
  for (auto it = floats.begin(); it != floats.end();) {
    it->t += dt;
    if (it->t > 1.1) it = floats.erase(it);
    else ++it;
  }
  for (auto it = toasts.begin(); it != toasts.end();) {
    it->left -= dt;
    if (it->left < -0.52) it = toasts.erase(it);
    else ++it;
  }
  hitmarker = std::max(0.0, hitmarker - dt);
  hurtFlash = std::max(0.0, hurtFlash - dt);
  zoneExpand = std::max(0.0, zoneExpand - dt);
  if (death && deathDelay > 0) deathDelay = std::max(0.0, deathDelay - dt);
}

}  // namespace rpg::client
