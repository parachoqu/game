#include "core/world/GameplayLayout.h"

#include <fstream>

#include <nlohmann/json.hpp>

#include "core/Paths.h"
#include "core/data/DataError.h"

namespace rpg {
namespace {

using Json = nlohmann::json;

const Json& at(const Json& j, std::string_view key, std::string_view where) {
  const auto it = j.find(key);
  if (it == j.end()) throw DataError("gameplay-layout.json › " + std::string(where) + ": falta '" + std::string(key) + "'");
  return *it;
}

double num(const Json& j, std::string_view key, std::string_view where) {
  const Json& v = at(j, key, where);
  if (!v.is_number()) throw DataError("gameplay-layout.json › " + std::string(where) + " › " + std::string(key) + ": esperado número");
  return v.get<double>();
}

std::string str(const Json& j, std::string_view key, std::string_view def = {}) {
  const auto it = j.find(key);
  if (it == j.end() || it->is_null()) return std::string(def);
  return it->get<std::string>();
}

XZ xz(const Json& j) {
  if (j.is_array()) return {j.at(0).get<double>(), j.at(1).get<double>()};
  return {j.at("x").get<double>(), j.at("z").get<double>()};
}

std::vector<XZ> points(const Json& j) {
  std::vector<XZ> out;
  for (const Json& p : j) out.push_back(xz(p));
  return out;
}

}  // namespace

GameplayLayout GameplayLayout::load(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) throw DataError("não foi possível abrir " + pathToUtf8(file));
  Json j;
  try {
    j = Json::parse(in);
  } catch (const Json::exception& e) {
    throw DataError("gameplay-layout.json: JSON inválido: " + std::string(e.what()));
  }
  try {
    GameplayLayout L;
    for (const Json& it : at(j, "interactables", "")) {
      LayoutInteractable x;
      x.kind = str(it, "kind");
      x.x = num(it, "x", "interactables");
      x.z = num(it, "z", "interactables");
      x.r = num(it, "r", "interactables");
      x.data = str(it, "data");
      x.node = it.value("node", -1);
      x.label = str(it, "label");
      L.interactables.push_back(std::move(x));
    }
    for (const Json& n : at(j, "nodes", "")) {
      L.nodes.push_back({str(n, "kind"), num(n, "x", "nodes"), num(n, "z", "nodes"), n.value("yieldBonus", 0),
                         n.value("rot", 0.0)});
    }
    for (const Json& n : at(j, "npcs", "")) {
      LayoutNpc x;
      x.x = num(n, "x", "npcs");
      x.z = num(n, "z", "npcs");
      x.yaw = n.value("yaw", 0.0);
      x.role = str(n, "role");
      x.face = n.value("face", false);
      x.work = n.value("work", false);
      x.race = str(n, "race", "humano");
      x.cloth = str(n, "cloth");
      x.trim = str(n, "trim");
      x.hood = str(n, "hood");
      x.weapon = str(n, "weapon");
      x.model = str(n, "model");
      x.name = str(n, "name");
      x.traveler = n.value("traveler", -1);
      L.npcs.push_back(std::move(x));
    }
    for (const Json& t : at(j, "travelers", "")) L.travelers.push_back({str(t, "key"), points(at(t, "path", "travelers"))});
    for (const Json& g : at(j, "groups", "")) {
      LayoutGroup x;
      x.id = str(g, "id");
      x.x = num(g, "x", "groups");
      x.z = num(g, "z", "groups");
      if (g.contains("leash") && !g["leash"].is_null()) x.leash = g["leash"].get<double>();
      x.respawn = num(g, "respawn", "groups");
      x.gorge = g.value("gorge", false);
      for (const Json& m : at(g, "members", "groups")) x.members.push_back({m.at(0).get<std::string>(), m.at(1).get<double>(), m.at(2).get<double>()});
      L.groups.push_back(std::move(x));
    }
    const auto post = [](const Json& p) { return LayoutPost{p.at("x").get<double>(), p.at("z").get<double>(), p.value("yaw", 0.0)}; };
    L.canteiroGuard = post(at(j, "canteiroGuard", ""));
    for (const Json& p : at(j, "guardPosts", "")) L.guardPosts.push_back(post(p));
    const Json& camp = at(j, "camp", "");
    L.campCenter = xz(at(camp, "center", "camp"));
    L.campDisplaced = xz(at(camp, "displaced", "camp"));
    L.campPatrol = points(at(camp, "patrol", "camp"));
    L.portalSpots = points(at(j, "portalSpots", ""));
    L.shelter = xz(at(j, "shelter", ""));
    const Json& t = at(j, "turbulent", "");
    L.turbulent.shrines = points(at(t, "shrines", "turbulent"));
    L.turbulent.exits = points(at(t, "exits", "turbulent"));
    L.turbulent.starts = points(at(t, "starts", "turbulent"));
    L.turbulent.portalCore = xz(at(t, "portalCore", "turbulent"));
    L.turbulent.center = xz(at(t, "center", "turbulent"));
    for (const Json& d : at(j, "discoveries", "")) {
      L.discoveries.push_back({str(d, "key"), num(d, "x", "discoveries"), num(d, "z", "discoveries"), num(d, "r", "discoveries"),
                               d.value("byZone", false)});
    }
    L.shortGorge = points(at(j, "shortGorge", ""));
    return L;
  } catch (const Json::exception& e) {
    throw DataError("gameplay-layout.json: " + std::string(e.what()));
  }
}

}  // namespace rpg
