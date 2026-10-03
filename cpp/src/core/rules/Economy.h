#pragma once
// Fórmulas de preço e reparo (economy.js) que o servidor aplica e as telas mostram: ficam aqui para
// as duas pontas calcularem o mesmo número.
#include <algorithm>
#include <cmath>

#include "core/Math.h"

namespace rpg::rules {

// economy.js `sellPrice`: base × procura × condição (equipamentos valem pelo desgaste, no mínimo 30%).
inline double sellPrice(double base, double demand, bool gear, double cond) {
  const double condK = gear ? std::max(0.3, cond / 100) : 1.0;
  return std::max(1.0, jsRound(base * demand * condK));
}

// economy.js `repairCost`: 2 moedas a cada 10% que falta; abaixo de 40% também um lingote.
struct RepairCost {
  double coins = 0;
  int lingote = 0;
};
inline RepairCost repairCost(double cond) {
  const double missing = 100 - cond;
  return {std::ceil(missing / 10) * 2, cond < 40 ? 1 : 0};
}

// event.js `PAY`: pagamento por entrega no canteiro (60% depois da reabertura).
inline double canteiroPay(bool ferramentas, bool reopened) {
  const double base = ferramentas ? 16 : 6;
  return reopened ? jsRound(base * 0.6) : base;
}

}  // namespace rpg::rules
