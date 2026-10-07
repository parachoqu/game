// Título e texto das entradas do Livro: os modelos de book.json com os argumentos que a simulação
// guardou, e as partes que book.js montava em código (plural, listas, constelações, encontros).
#include <algorithm>
#include <cmath>
#include <format>

#include "client/ui/Localization.h"
#include "core/GameClock.h"
#include "core/world/SkyLayout.h"

namespace rpg::client {

namespace {

using protocol::MsgArg;
using protocol::MsgArgs;

std::string lowerAll(std::string s) {
  for (std::size_t i = 0; i < s.size(); ++i) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c >= 'A' && c <= 'Z') {
      s[i] = static_cast<char>(c + 32);
    } else if (c == 0xC3 && i + 1 < s.size()) {
      const auto d = static_cast<unsigned char>(s[i + 1]);
      if (d >= 0x80 && d <= 0x9E && d != 0x97) s[i + 1] = static_cast<char>(d + 0x20);
      ++i;
    }
  }
  return s;
}

std::string capFirst(std::string s) {
  if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 32);
  return s;
}

int intArg(const MsgArgs& a, std::size_t i) { return i < a.size() ? static_cast<int>(std::lround(a[i].number)) : 0; }

const SkyLayout& sky() {
  static const SkyLayout layout = SkyLayout::generate();
  return layout;
}

std::string constellationName(int c) {
  return c >= 0 && static_cast<std::size_t>(c) < sky().constellations.size() ? sky().constellations[static_cast<std::size_t>(c)].key : std::string{};
}

// stars.js `starWhere` / `starLabel` e o título curto do registro noturno
std::string skyPart(const Localization& L, const char* kind, int constellation) {
  return constellation < 0 ? L.bookPart(std::string("sky.") + kind + ".isolated", {})
                           : L.bookPart(std::string("sky.") + kind + ".constellation", {MsgArg::str(constellationName(constellation))});
}

// turbulent.js `encTxt`: arma (item) e desfecho, aos pares
std::string encounters(const Localization& L, const MsgArgs& a, std::size_t from) {
  std::string out;
  for (std::size_t i = from; i + 1 < a.size(); i += 2) {
    std::string e = L.bookPart("encounter", {MsgArg::str(lowerAll(L.arg(a[i])))});
    switch (intArg(a, i + 1)) {
      case 1: e += " (" + L.bookPart("encounter.fell", {}) + ")"; break;
      case 2: e += " (" + L.bookPart("encounter.stood", {}) + ")"; break;
      case 3: e += " (" + L.bookPart("encounter.nooutcome", {}) + ")"; break;
      default: break;
    }
    if (!out.empty()) out += "; ";
    out += e;
  }
  return out.empty() ? L.bookPart("encounter.none", {}) : out;
}

std::string when(double time) {
  const ClockTime c = clockAt(time, ClockConfig{});
  return std::format("Dia {} · {:02d}h", c.day, static_cast<int>(std::floor(c.hour)));
}

struct Texts {
  std::string key;  // modelo efetivo (variantes .1 / .10)
  MsgArgs args;
};

// Escolhe a variante do modelo e monta os argumentos já escritos.
Texts resolve(const Localization& L, const GameData& data, const protocol::BookEntry& e) {
  const MsgArgs& a = e.args;
  const std::string& t = e.tmpl;
  const auto tally = [&](const std::string& k) -> double {
    for (const auto& [dk, v] : e.data)
      if (dk == k) return v;
    return 0;
  };
  if (t == "first.chegada") {
    std::string start = a.size() > 1 ? L.arg(a[1]) : std::string{}, weapon;
    if (a.size() > 1 && data.starts.find(a[1].text).valid()) {
      const StartId id = data.starts.find(a[1].text);
      start = L.start(id);
      if (data.starts[id].weapon.valid()) weapon = lowerAll(L.item(data.starts[id].weapon));
    }
    return {t, {a.empty() ? MsgArg::str("") : a[0], MsgArg::str(start), MsgArg::str(weapon)}};
  }
  if (t == "first.venda") {
    std::string market = a.size() > 1 ? a[1].text : std::string{};
    if (data.markets.find(market).valid()) market = L.market(data.markets.find(market));
    return {t, {MsgArg::str(a.empty() ? std::string{} : lowerAll(L.arg(a[0]))), MsgArg::str(market)}};
  }
  if (t == "first.treino") {
    const TrainingId id{a.empty() ? std::uint16_t{0} : a[0].id};
    return {t, {MsgArg::str(L.training(id)), MsgArg::str(data.trainings.contains(id) ? L.trainingDesc(id) : std::string{})}};
  }
  if (t == "first.primeira-derrota") return {t, a};
  if (t == "log.derrota") {
    // (lugar, causa, zona, linhas perdidas, qtd perdida, linhas destruídas, qtd destruída)
    std::string lost;
    if (intArg(a, 3) || intArg(a, 5))
      lost = L.bookPart("log.derrota.lost", {MsgArg::integer(intArg(a, 4)),
                                             MsgArg::str(intArg(a, 5) ? L.bookPart("log.derrota.destroyed", {MsgArg::integer(intArg(a, 6))}) : "")});
    else
      lost = L.bookPart("log.derrota.nothing", {});
    return {t, {a.size() > 0 ? a[0] : MsgArg::str(""), MsgArg::str(a.size() > 1 ? lowerAll(L.arg(a[1])) : ""), a.size() > 2 ? a[2] : MsgArg::str(""),
                MsgArg::str(lost)}};
  }
  if (t == "tally.entregas-alto") {
    if (e.count <= 1) return {t + ".1", {}};
    return {t, {MsgArg::integer(e.count), MsgArg::real(tally("total"))}};
  }
  if (t == "tally.oficio") {
    std::string parts;
    for (const auto& [id, q] : e.data) {
      if (!parts.empty()) parts += ", ";
      const std::string name = data.items.find(id).valid() ? lowerAll(L.item(data.items.find(id))) : id;
      parts += Localization::number(q) + " " + name;
    }
    return {e.count >= 10 ? t + ".10" : t, {MsgArg::str(parts)}};
  }
  if (t == "tally.reparos") return {t, {MsgArg::integer(e.count), MsgArg::str(e.count > 1 ? "vezes" : "vez")}};
  if (t == "tally.montaria" || t == "tally.resgates") return {e.count > 1 ? t : t + ".1", {MsgArg::integer(e.count)}};
  if (t.rfind("tally.", 0) == 0) return {t, {MsgArg::integer(e.count)}};
  if (t == "log.reabertura") {
    // (ferramentas, lingotes, reconhecimento?, saqueadores?, acampamento?, %)
    std::string parts;
    const auto add = [&](const std::string& s) { parts += (parts.empty() ? "" : ", ") + s; };
    if (intArg(a, 0)) add(L.bookPart("log.reabertura.ferramentas", {MsgArg::integer(intArg(a, 0))}));
    if (intArg(a, 1)) add(L.bookPart("log.reabertura.lingote", {MsgArg::integer(intArg(a, 1))}));
    if (intArg(a, 2)) add(L.bookPart("log.reabertura.reconhecimento", {}));
    if (intArg(a, 3)) add(L.bookPart("log.reabertura.saqueadores", {}));
    if (intArg(a, 4)) add(L.bookPart("log.reabertura.acampamento", {}));
    return {t, {MsgArg::str(parts.empty() ? "" : capFirst(parts) + ". "), MsgArg::integer(intArg(a, 5))}};
  }
  if (t == "log.obs-dia" || t == "log.obs-noite") {
    // (estrela, constelação, instante)
    const int c = intArg(a, 1);
    const std::string w = when(a.size() > 2 ? a[2].number : e.time);
    if (t == "log.obs-dia") return {t, {MsgArg::str(skyPart(L, "where", c)), MsgArg::str(w)}};
    return {t, {MsgArg::str(skyPart(L, "short", c)), MsgArg::str(skyPart(L, "label", c)), MsgArg::str(w)}};
  }
  if (t == "log.incursao") {
    const int frags = intArg(a, 0);
    return {t, {MsgArg::integer(frags), MsgArg::str(frags == 1 ? "" : "s"), MsgArg::str(encounters(L, a, 1))}};
  }
  if (t == "log.incursao-perdida") return {t, {MsgArg::str(encounters(L, a, 0))}};
  return {t, a};
}

}  // namespace

std::string Localization::bookTitle(const protocol::BookEntry& e) const {
  if (e.literal) return e.literalTitle;
  if (e.tmpl.rfind("first.disc-", 0) == 0) return field("discoveries", e.tmpl.substr(11), "title");
  const Texts r = resolve(*this, *data_, e);
  return bookPart(r.key, r.args, true);
}

std::string Localization::bookText(const protocol::BookEntry& e) const {
  if (e.literal) return e.literalText;
  if (e.tmpl.rfind("first.disc-", 0) == 0) return field("discoveries", e.tmpl.substr(11), "text");
  const Texts r = resolve(*this, *data_, e);
  return bookPart(r.key, r.args, false);
}

}  // namespace rpg::client
