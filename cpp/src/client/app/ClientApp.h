#pragma once
// Aplicação do cliente (main.js): laço principal e telas (carregando → título → criação → jogo).
// Só fala com o servidor pela ClientSession; nunca toca a simulação.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "net/Transport.h"

namespace rpg {
struct GameData;
class StaticWorld;
}  // namespace rpg

namespace rpg::client {

struct AppOptions {
  std::filesystem::path dataDir;    // cpp/data
  std::filesystem::path assetsDir;  // cpp/assets (fontes)
  int width = 1280, height = 720;
  bool fullscreen = false;
  bool headless = false;            // sem janela (captura e testes)
  bool vsync = true;
  std::optional<std::filesystem::path> screenshot;  // grava o último quadro em PNG
  int frames = 0;                   // encerra depois de N quadros (0 = até fechar a janela)
  std::optional<std::string> view;  // enquadramento das capturas da demo (02_mercado…)
  std::optional<double> hour;       // força a hora da luz (capturas)
  bool autoStart = false;           // pula título e criação (personagem padrão)
  bool autoContinue = false;         // pula o título e retoma o último personagem ("Continuar trajetória")
  std::optional<std::string> quality;  // força o nível (alta, media, baixa) sem salvar
  // tools/measure-benchmark.mjs: seis pontos nos três níveis; mediana, p95, triângulos e chamadas
  bool benchmark = false;
  int benchFrames = 30;
  std::filesystem::path benchOut = "benchmark-cpp.json";
  bool hideHud = false;
  bool portrait = false;            // câmera de frente para o próprio personagem (capturas da animação)
  bool dev = false;                 // comandos de desenvolvimento (tp, give…)
  bool debugUi = false;             // abre as ferramentas de depuração (F3) já no começo
  double fixedDt = 0;               // passo fixo do cliente (capturas reproduzíveis); 0 = relógio real
  std::string name, origin, start, model;  // personagem do autoStart
  // Fase 8: resumo dos dados de design (vai no Hello; o servidor recusa se não bater) e predição do
  // próprio personagem (ligada no jogo pela rede ou com latência simulada).
  std::string dataHash;
  bool predict = false;
  // Preferências e último personagem (Settings); vazio = só na memória (capturas e testes).
  std::filesystem::path settingsFile;
  // Há save para este nome? (jogo local: o servidor do mesmo processo). Sem a função, o título confia
  // no último personagem das preferências e o servidor decide.
  std::function<bool(const std::string& name)> hasSave;
};

class ClientApp {
 public:
  ClientApp(const GameData& data, const StaticWorld& statics, AppOptions options, std::unique_ptr<net::ITransport> transport);
  ~ClientApp();
  ClientApp(const ClientApp&) = delete;
  ClientApp& operator=(const ClientApp&) = delete;

  // Roda até fechar; devolve o código de saída do processo.
  int run();

  // Enquadramentos das capturas da demo (tools/capture-demo-views.mjs).
  static bool knownView(const std::string& name);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace rpg::client
