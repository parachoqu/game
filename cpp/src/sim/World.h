#pragma once
// O mundo autoritativo. Substitui o estado global `G` da demo (state.js): tudo o que a simulação sabe
// mora aqui e é passado explicitamente. Não conhece câmera, tela, teclado, som nem texto de interface.
//
// Nesta fase o mundo tem o mapa estático (relevo, colisores, zonas), tempo, relógio e gerador com
// semente. Os sistemas entram na fase 2, na mesma ordem do `tick()` de main.js: jogadores →
// montarias → inimigos → grupos → projéteis → acampamento → evento → estrelas → Turbulenta →
// mercados → cargas → NPCs → descobertas.
#include <cstdint>

#include "core/GameClock.h"
#include "core/Rng.h"
#include "core/Types.h"
#include "core/data/GameData.h"
#include "core/world/StaticWorld.h"

namespace rpg::sim {

class World {
 public:
  // `data` e `statics` são carregados uma vez e precisam viver mais que o World.
  World(const GameData& data, const StaticWorld& statics, std::uint64_t seed);

  // Avança um passo fixo.
  void step(Seconds dt);

  const GameData& data() const { return data_; }
  const StaticWorld& statics() const { return statics_; }
  Tick tick() const { return tick_; }
  Seconds time() const { return time_; }  // G.time: segundos de jogo desde o início
  ClockTime clock() const { return clockAt(time_, data_.balance.clock); }
  Pcg32& rng() { return rng_; }

  // Ajuste direto do tempo (carregar save, comandos de desenvolvimento como `night`).
  void setTime(Seconds t) { time_ = t; }

 private:
  const GameData& data_;
  const StaticWorld& statics_;
  Pcg32 rng_;
  Tick tick_ = 0;
  Seconds time_ = 0.0;
};

}  // namespace rpg::sim
