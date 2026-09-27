#include "core/world/SimPack.h"

#include <fstream>
#include <string_view>

#include <nlohmann/json.hpp>

#include "core/BinaryReader.h"
#include "core/data/DataError.h"

namespace rpg {
namespace {

using Json = nlohmann::ordered_json;
namespace fs = std::filesystem;

// `where` é string_view: com std::string temporária o GCC 13 acusa (falsamente) referência pendente
// no retorno de `obj`.
[[noreturn]] void fail(std::string_view where, const char* key, const char* what) {
  throw DataError("simpack.json › " + std::string(where) + " › " + key + ": " + what);
}

double num(const Json& j, const char* key, std::string_view where) {
  if (!j.contains(key) || !j.at(key).is_number()) fail(where, key, "esperado número");
  return j.at(key).get<double>();
}
int integer(const Json& j, const char* key, std::string_view where) {
  if (!j.contains(key) || !j.at(key).is_number_integer()) fail(where, key, "esperado inteiro");
  return j.at(key).get<int>();
}
std::string str(const Json& j, const char* key, std::string_view where) {
  if (!j.contains(key) || !j.at(key).is_string()) fail(where, key, "esperado texto");
  return j.at(key).get<std::string>();
}
const Json& obj(const Json& j, const char* key, std::string_view where) {
  if (!j.contains(key) || !j.at(key).is_object()) fail(where, key, "esperado objeto");
  return j.at(key);
}

Heightfield::Meta heightMeta(const Json& h, std::string_view where) {
  Heightfield::Meta m;
  m.w = integer(h, "w", where);
  m.h = integer(h, "h", where);
  m.step = num(h, "step", where);
  m.x0 = num(h, "x0", where);
  m.y0 = num(h, "y0", where);
  m.min = num(h, "min", where);
  m.scale = num(h, "scale", where);
  return m;
}

std::vector<Polyline> polylines(const Json& routes, std::string_view where) {
  std::vector<Polyline> out;
  for (const auto& [id, r] : routes.items()) {
    Polyline p;
    p.id = id;
    p.width = num(r, "width", std::string(where) + " › " + id);
    for (const auto& pt : r.at("pts")) p.pts.push_back({pt.at(0).get<double>(), pt.at(1).get<double>()});
    out.push_back(std::move(p));
  }
  return out;
}

std::vector<Circle> circles(const Json& list) {
  std::vector<Circle> out;
  for (const auto& c : list) out.push_back({c.at("x").get<double>(), c.at("z").get<double>(), c.at("r").get<double>()});
  return out;
}

std::vector<Collider> readColliders(const fs::path& path, std::size_t expected) {
  const auto bytes = readFileBytes(path);
  ByteCursor c(bytes, path.filename().string());
  if (c.ascii(4) != "RPGC") throw DataError(path.filename().string() + ": assinatura inválida");
  if (const auto v = c.u32(); v != 1) throw DataError(path.filename().string() + ": versão " + std::to_string(v) + " desconhecida");
  const std::uint32_t count = c.u32();
  if (count != expected) throw DataError(path.filename().string() + ": quantidade diferente da declarada em simpack.json");
  std::vector<Collider> out;
  out.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    Collider o;
    const std::uint8_t t = c.u8();
    if (t == 0) {
      o.type = Collider::Type::Circle;
      o.x = c.f64();
      o.z = c.f64();
      o.r = c.f64();
    } else if (t == 1) {
      o.type = Collider::Type::Box;
      o.x = c.f64();
      o.z = c.f64();
      o.hw = c.f64();
      o.hd = c.f64();
      o.c = c.f64();
      o.s = c.f64();
      o.r = c.f64();
    } else {
      throw DataError(path.filename().string() + ": tipo de colisor desconhecido");
    }
    out.push_back(o);
  }
  if (c.remaining() != 0) throw DataError(path.filename().string() + ": bytes sobrando no fim");
  return out;
}

}  // namespace

SimPack SimPack::load(const fs::path& dir) {
  Json j;
  {
    std::ifstream in(dir / "simpack.json", std::ios::binary);
    if (!in) throw DataError("não foi possível abrir " + (dir / "simpack.json").string());
    try {
      j = Json::parse(in);
    } catch (const Json::exception& e) {
      throw DataError(std::string("simpack.json: JSON inválido: ") + e.what());
    }
  }

  SimPack p;
  p.schema = integer(j, "schema", "raiz");
  if (p.schema != 1) throw DataError("simpack.json: schema " + std::to_string(p.schema) + " desconhecido (esperado 1)");
  const Json& source = obj(j, "source", "raiz");
  p.worldLayoutVersion = integer(source, "worldLayoutVersion", "source");
  p.runtimeSchema = str(source, "runtimeSchema", "source");

  const Json& coords = obj(j, "coordinates", "raiz");
  p.planZSign = num(coords, "planZSign", "coordinates");
  const Json& off = obj(coords, "turbulentOffset", "coordinates");
  p.turbulentOffsetX = num(off, "x", "turbulentOffset");
  p.turbulentOffsetY = num(off, "y", "turbulentOffset");
  p.turbulentOffsetZ = num(off, "z", "turbulentOffset");
  p.turbulentGateX = num(coords, "turbulentGateX", "coordinates");

  // ---- região
  const Json& region = obj(j, "region", "raiz");
  p.region.half = num(region, "half", "region");
  const Json& rh = obj(region, "height", "region");
  p.region.heightMeta = heightMeta(rh, "region.height");
  const auto rCount = static_cast<std::size_t>(p.region.heightMeta.w) * static_cast<std::size_t>(p.region.heightMeta.h);
  p.region.height = readU16File(dir / str(rh, "file", "region.height"), rCount);

  const Json& mask = obj(region, "mask", "region");
  p.region.maskW = integer(mask, "w", "region.mask");
  const auto maskCells = static_cast<std::size_t>(p.region.maskW) * static_cast<std::size_t>(p.region.maskW);
  p.region.mask = readU8File(dir / str(mask, "file", "region.mask"), (maskCells + 1) / 2);

  const Json& river = obj(region, "river", "region");
  p.region.riverX0 = num(river, "x0", "region.river");
  const auto n = static_cast<std::size_t>(integer(river, "n", "region.river"));
  const auto riverAll = readF32File(dir / str(river, "file", "region.river"), n * 3);
  p.region.riverLevel.assign(riverAll.begin(), riverAll.begin() + static_cast<std::ptrdiff_t>(n));
  p.region.riverCenter.assign(riverAll.begin() + static_cast<std::ptrdiff_t>(n), riverAll.begin() + static_cast<std::ptrdiff_t>(2 * n));
  p.region.riverHalf.assign(riverAll.begin() + static_cast<std::ptrdiff_t>(2 * n), riverAll.end());

  const Json& rf = obj(region, "routeField", "region");
  p.region.routeN = integer(rf, "n", "region.routeField");
  p.region.routeStep = num(rf, "step", "region.routeField");
  p.region.routeOrigin = num(rf, "origin", "region.routeField");
  p.region.routeField = readU8File(dir / str(rf, "file", "region.routeField"),
                                   static_cast<std::size_t>(p.region.routeN) * static_cast<std::size_t>(p.region.routeN));
  p.region.routes = polylines(obj(region, "routes", "region"), "region.routes");

  // ---- Turbulenta
  const Json& turb = obj(j, "turbulent", "raiz");
  p.turbulent.half = num(turb, "half", "turbulent");
  const Json& th = obj(turb, "height", "turbulent");
  p.turbulent.heightMeta = heightMeta(th, "turbulent.height");
  p.turbulent.height = readU16File(dir / str(th, "file", "turbulent.height"),
                                   static_cast<std::size_t>(p.turbulent.heightMeta.w) * static_cast<std::size_t>(p.turbulent.heightMeta.h));
  p.turbulent.routes = polylines(obj(turb, "routes", "turbulent"), "turbulent.routes");

  // ---- pontes, colisores, lugares e zonas
  for (const auto& b : j.at("bridges")) {
    Bridge br;
    br.name = str(b, "name", "bridges");
    br.x = num(b, "x", br.name);
    br.z = num(b, "z", br.name);
    br.hw = num(b, "hw", br.name);
    br.hd = num(b, "hd", br.name);
    br.y = num(b, "y", br.name);
    br.rot = num(b, "rot", br.name);
    br.c = num(b, "c", br.name);
    br.s = num(b, "s", br.name);
    br.apron = num(b, "apron", br.name);
    br.apronSide = num(b, "apronSide", br.name);
    p.bridges.push_back(std::move(br));
  }

  const Json& col = obj(j, "colliders", "raiz");
  p.colliders = readColliders(dir / str(col, "file", "colliders"), static_cast<std::size_t>(integer(col, "count", "colliders")));

  for (const auto& [key, l] : obj(j, "places", "raiz").items()) {
    PlaceArea a{num(l, "x", key), num(l, "z", key), num(l, "r", key), 0, 0};
    if (l.contains("rx")) {
      a.rx = num(l, "rx", key);
      a.rz = num(l, "rz", key);
    }
    p.places.emplace(key, a);
  }
  const Json& zones = obj(j, "zones", "raiz");
  p.protectedPoints = circles(zones.at("protected"));
  p.neverProtected = circles(zones.at("neverProtected"));
  return p;
}

}  // namespace rpg
