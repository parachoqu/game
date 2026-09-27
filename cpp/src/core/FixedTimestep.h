#pragma once
// Acumulador de passo fixo. O servidor avança a simulação sempre com o mesmo dt (1/30 s por padrão,
// o mesmo `step(frames, 1/30)` dos testes da demo), não importa a taxa de quadros de quem o hospeda.
//
// A demo limitava o dt do quadro a 0,05 s; aqui o equivalente é `maxSteps`: depois de uma pausa longa
// (depurador, máquina travada) a simulação não tenta recuperar todo o tempo perdido de uma vez.
#include "core/Types.h"

namespace rpg {

class FixedTimestep {
 public:
  explicit FixedTimestep(Seconds step = 1.0 / 30.0, int maxSteps = 8) : step_(step), maxSteps_(maxSteps) {}

  // Soma o tempo real decorrido e devolve quantos passos fixos devem rodar agora.
  int advance(Seconds elapsed);

  // Fração do próximo passo já acumulada, em [0, 1): o cliente usa para interpolar.
  double alpha() const { return accumulator_ / step_; }

  Seconds step() const { return step_; }
  Seconds dropped() const { return dropped_; }  // tempo descartado por excesso de atraso

 private:
  Seconds step_;
  int maxSteps_;
  Seconds accumulator_ = 0.0;
  Seconds dropped_ = 0.0;
};

}  // namespace rpg
