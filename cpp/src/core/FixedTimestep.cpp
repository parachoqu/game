#include "core/FixedTimestep.h"

#include <cmath>

namespace rpg {

int FixedTimestep::advance(Seconds elapsed) {
  if (elapsed > 0.0) accumulator_ += elapsed;
  int steps = 0;
  while (accumulator_ >= step_ && steps < maxSteps_) {
    accumulator_ -= step_;
    ++steps;
  }
  if (accumulator_ >= step_) {  // atraso grande demais: descarta o excedente, guarda só a fração
    const Seconds keep = std::fmod(accumulator_, step_);
    dropped_ += accumulator_ - keep;
    accumulator_ = keep;
  }
  return steps;
}

}  // namespace rpg
