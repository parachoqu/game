#pragma once
// Colisores estáticos (círculos e caixas) em grade espacial (game/collide.js).
//
// A ordem de inserção importa: `resolve` empurra o ponto para fora de cada colisor da célula em
// sequência, então a mesma lista em outra ordem daria outro resultado. O pacote de simulação traz
// os colisores na ordem em que a demo os cria.
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "core/Types.h"

namespace rpg {

struct Collider {
  enum class Type : std::uint8_t { Circle = 0, Box = 1 };
  Type type = Type::Circle;
  double x = 0, z = 0;
  double r = 0;           // círculo: raio; caixa: raio envolvente (hypot(hw, hd))
  double hw = 0, hd = 0;  // caixa: meias extensões no espaço local
  double c = 1, s = 0;    // caixa: cos/sen de −rotY
};

class CollisionGrid {
 public:
  explicit CollisionGrid(double cell = 12.0) : cell_(cell) {}

  void addCircle(double x, double z, double r);
  // rotY no mesmo sentido de Object3D.rotation.y (collide.js addBox)
  void addBox(double x, double z, double hw, double hd, double rotY = 0.0);
  // Inserção com todos os campos prontos (vindos do pacote, calculados pelo JS).
  void add(const Collider& c);

  // Empurra um círculo (x, z, r) para fora dos colisores da célula do ponto (collide.js resolve).
  XZ resolve(double x, double z, double r) const;
  // O ponto, com margem r, está dentro de algum colisor? (collide.js testPoint)
  bool testPoint(double x, double z, double r = 0.4) const;

  std::size_t size() const { return colliders_.size(); }
  const std::vector<Collider>& colliders() const { return colliders_; }

 private:
  static std::int64_t key(std::int64_t i, std::int64_t j) { return i * 100003 + j; }  // collide.js `key`
  const std::vector<std::uint32_t>* cellAt(double x, double z) const;

  double cell_;
  std::vector<Collider> colliders_;
  std::unordered_map<std::int64_t, std::vector<std::uint32_t>> cells_;
};

}  // namespace rpg
