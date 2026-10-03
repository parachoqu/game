#pragma once
// AnimationMixer do three com só o que a demo usa: ações tocando em laço (LoopRepeat), peso efetivo
// sem fades e tempo posto à mão. O resultado é o mesmo do mixer da demo:
//   - as ações avançam o tempo na ordem em que foram ativadas, mesmo com peso zero;
//   - cada propriedade animada acumula os valores das ações com peso > 0 (o primeiro é copiado, os
//     seguintes entram por slerp/lerp com fração w/W) e o que falta até 1 é completado com a pose
//     original (a de repouso, que o PropertyMixer guarda ao criar a ligação);
//   - propriedades que nenhum clipe anima ficam com o que a pose já tinha.
// Avaliar a pose a partir do repouso a cada quadro equivale ao `mixerStep` de characters.js, que
// devolve os ossos com pose procedural ao valor deixado pelo mixer antes de cada passo.
#include <cstdint>
#include <vector>

#include "client/anim/Rig.h"

namespace rpg::client::anim {

struct Action {
  const Clip* clip = nullptr;
  double time = 0;
  double timeScale = 1;
  double weight = 0;  // setEffectiveWeight
  int loopCount = -1;
  std::vector<int> bindings;  // por trilha do clipe
};

class Mixer {
 public:
  explicit Mixer(const Rig& rig);

  // clipAction(clip).play(): devolve o índice da ação (ordem de ativação).
  int add(const Clip* clip);
  Action& action(int i) { return actions_[static_cast<std::size_t>(i)]; }
  const Action& action(int i) const { return actions_[static_cast<std::size_t>(i)]; }
  std::size_t size() const { return actions_.size(); }

  // mixer.update(dt): avança os tempos e escreve as propriedades ligadas em `pose`.
  void update(double dt, Pose& pose);

 private:
  struct Binding {
    int node = -1;
    Track::Path path = Track::Path::Position;
    double original[4] = {0, 0, 0, 1};
    double accum[4] = {0, 0, 0, 1};
    double weight = 0;
  };
  int binding(int node, Track::Path path);
  static double updateTime(Action& a, double dt);

  const Rig& rig_;
  std::vector<Action> actions_;
  std::vector<Binding> bindings_;
};

}  // namespace rpg::client::anim
