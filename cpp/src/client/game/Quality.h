#pragma once
// Qualidade gráfica (engine/quality.js): Alta / Média / Baixa, troca ao vivo e ajuste automático nos
// primeiros segundos. O nível controla o pós-processamento, a sombra do sol (tamanho do mapa e
// alcance), a grama e a cobertura do chão, a densidade do sub-bosque e o alcance do cenário (blocos de
// relevo, nível de detalhe das árvores e arbustos).
#include <array>
#include <optional>
#include <string_view>
#include <vector>

#include "client/world/PackScene.h"

namespace rpg::client {

struct QualityLevel {
  std::string_view key, label;
  double dpr = 1;  // teto do devicePixelRatio (a janela nativa já desenha em pixels reais: só relatórios)
  bool post = false, gtao = false, bloom = false, smaa = false;
  int shadow = 1024;  // lado do mapa de sombra do sol
  double grass = 1, grassFar = 80;
  double vegetation = 1, secondary = 1;
  bool vegShadow = false;
  double tileNear = 300, blockFar = 1600, shadowSpan = 70, shadowFar = 340, turbFx = 1;
  double natureFar = 360, canopyFar = 700, groundFar = 140, extraFar = 900;
  std::array<float, 4> treeLod{};   // L0, L1, L2 e impostor; 0 desliga o nível
  std::array<float, 3> shrubLod{};
  double vegShadowFar = 0, coverRadius = 84, coverDensity = 1;
};

// LEVELS na ordem de quality.js (ORDER: alta, media, baixa).
const std::array<QualityLevel, 3>& qualityLevels();
// Nível pela chave; chave desconhecida dá "alta".
const QualityLevel& qualityLevel(std::string_view key);
// Próximo nível abaixo (nada se já é o mais baixo).
std::optional<std::string_view> lowerQuality(std::string_view key);

// Alcances do cenário que o WorldView aplica (applyQuality → setLODBands, setSecondaryDensity…).
WorldDetail detailFor(const QualityLevel& q);

// quality.js `sampleFrame`: ignora o aquecimento, mede ~4 s; se a mediana passar de 30 ms, desce um
// nível (até duas vezes). Quadros acima de 250 ms (troca de janela, compilação) não contam.
class AutoQuality {
 public:
  // `chosen`: o jogador escolheu o nível (QUALITY.chosen): não há ajuste.
  explicit AutoQuality(bool chosen = false) : done_(chosen) {}
  // Um quadro de `ms`; devolve o nível novo quando é hora de descer.
  std::optional<std::string_view> sample(double ms, std::string_view current, bool visible = true);
  void stop() { done_ = true; }
  bool active() const { return !done_; }

 private:
  std::vector<double> samples_;
  int warm_ = 0, steps_ = 0;
  bool done_ = false;
};

}  // namespace rpg::client
