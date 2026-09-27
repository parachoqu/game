#pragma once
// Motor do personagem (partes de movimento de game/player.js `updatePlayer` e `startJump`).
//
// Compartilhado: o servidor usa com autoridade e o cliente para prever o próprio jogador. Cada função
// é um trecho do JS na mesma ordem de operações, para o resultado sair igual bit a bit:
//
//   moveSpeed   — velocidade do passo (andar, correr, agachar, pouso, mira, fera, carga, montado, vau)
//   startJump   — impulso do pulo e do salto impulsionado
//   airSteer    — controle aéreo parcial do passo no ar
//   applyGravity— queda e pouso
//   bodyHeight  — altura final no chão ou na água
#include <cstdint>
#include <optional>

#include "core/Types.h"
#include "core/data/Defs.h"
#include "core/world/StaticWorld.h"

namespace rpg::motor {

// Constantes de regra do movimento que não estão em config.js (ficavam soltas em player.js).
inline constexpr double kLandSlow = 0.6;       // amortecimento do pouso
inline constexpr double kAimSlow = 0.62;       // mirando, o passo é curto
inline constexpr double kBeastFast = 1.25;     // Forma da Fera
inline constexpr double kOverloadSlow = 0.6;   // sobrecarga
inline constexpr double kLimitSlow = 0.35;     // no limite de carga
inline constexpr double kMountedSpeed = 12.5;  // montado, carga normal
inline constexpr double kMountedLoaded = 9.0;  // montado com peso acima do normal
inline constexpr double kWadeSlow = 0.55;      // dentro do rio
inline constexpr double kWadeDepth = 0.35;     // profundidade a partir da qual conta como vau
inline constexpr double kWadeFloat = 0.9;      // no vau o corpo não afunda mais que isso
inline constexpr double kLandBoosted = 0.2;    // pouso do salto impulsionado (s)
inline constexpr double kLandNormal = 0.12;    // pouso do pulo curto (s)
inline constexpr double kBodyRadius = 0.45;
inline constexpr double kMountedRadius = 1.0;

enum class LoadState : std::uint8_t { Normal, Overloaded, Limit };  // normal, sobrecarga, limite

// P.air da demo.
struct Air {
  double y = 0, vy = 0, vx = 0, vz = 0;
  bool boosted = false;
  double t = 0, dur = 0;
  bool operator==(const Air&) const = default;
};

struct SpeedInputs {
  bool running = false;
  bool crouch = false;
  bool aiming = false;
  bool metamorph = false;  // metamorphT > 0
  bool mounted = false;
  bool wading = false;
  LoadState load = LoadState::Normal;
  double armorSpeed = 1.0;  // inventory.js armorSpeed(P)
};

// Velocidade do passo. `landT` é o amortecimento do pouso: decrementado aqui, como no JS.
double moveSpeed(const Balance::Movement& m, const SpeedInputs& in, double& landT, double dt);

// Profundidade da água sob o corpo (antes do passo): `surface` e se conta como vau.
struct Wading {
  double surface = 0;
  bool wading = false;
};
Wading wadingAt(const StaticMap& map, double x, double z);

// Novo salto. `mx, mz`: direção do passo (0, 0 parado); `sp`: velocidade atual.
Air startJump(const Balance::Movement& m, bool boosted, double mx, double mz, double sp, double yaw, double armorSpeed);

// No ar, o comando corrige só uma parte do impulso (AIR_CONTROL) e o corpo anda com colisão.
void airSteer(const Balance::Movement& m, const StaticMap& map, Air& air, XZ& pos, bool moving, double mx, double mz,
              double sp, double dt);

// Gravidade em qualquer estado. Devolve true se pousou neste passo (e zera `air`, ajusta `landT`).
bool applyGravity(const Balance::Movement& m, std::optional<Air>& air, double& landT, double dt);

// Altura final do corpo: chão (ou piso da ponte) + salto; no vau, não afunda mais que kWadeFloat.
double bodyHeight(const StaticMap& map, double x, double z, const std::optional<Air>& air, const Wading& w);

}  // namespace rpg::motor
