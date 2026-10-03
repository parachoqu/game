#pragma once
// Decoradores próprios da interface (o que styles.css desenha e o RmlUi não tem):
//   rivets — os rebites de bronze nos quatro cantos das peças forjadas (`--rivets` da demo: quatro
//            radial-gradient de 10 px a 6 px das bordas);
// e a textura do grão de pedra (`--grain`, um SVG de ruído a 5 %), gerada como textura dinâmica
// `dyn:grain` para o decorador image(... repeat).
#include <cstdint>
#include <memory>
#include <vector>

namespace Rml {
class DecoratorInstancer;
}

namespace rpg::client {

class RmlRender;

// Registra os decoradores no RmlUi (depois de Rml::Initialise). Os instanciadores precisam viver até
// Rml::Shutdown: quem chama guarda o que volta.
std::vector<std::unique_ptr<Rml::DecoratorInstancer>> registerUiDecorators();

// Grão de pedra: ruído cinza com alfa baixo, pré-multiplicado (120 × 120, determinístico).
std::vector<std::uint8_t> stoneGrain(int size);

}  // namespace rpg::client
