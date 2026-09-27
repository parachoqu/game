#pragma once
// Relógio do mundo (state.js `clock()`): dia e hora a partir do tempo de jogo.
//
// O rótulo "Dia 2 · 14h" é texto de interface e fica no cliente; a simulação só trabalha com números.
#include "core/Types.h"

namespace rpg {

struct ClockConfig {
  Seconds dayLength = 420.0;  // segundos reais por dia de jogo (config.js DAY_LENGTH)
  double startHour = 9.0;     // hora do início (config.js START_HOUR)
};

struct ClockTime {
  int day = 1;        // começa em 1
  double hour = 0.0;  // [0, 24)
};

// Mesma conta do JS: total = t + (START_HOUR / 24) · DAY_LENGTH; dia = ⌊total / DAY_LENGTH⌋ + 1;
// hora = (total mod DAY_LENGTH) / DAY_LENGTH · 24.
ClockTime clockAt(Seconds gameTime, const ClockConfig& cfg);

// Tempo de jogo que falta até a próxima vez que o relógio marcar `hour` (0 se já é essa hora).
// Usado pelo comando de depuração `night` da demo: G.time += ((22 − hora + 24) % 24) / 24 · DAY_LENGTH.
Seconds secondsUntilHour(Seconds gameTime, double hour, const ClockConfig& cfg);

}  // namespace rpg
