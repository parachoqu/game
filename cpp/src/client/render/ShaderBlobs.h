#pragma once
// SPIR-V embutido no executável (gerado de shaders/src por shaders/CMakeLists.txt).
#include <cstdint>
#include <span>
#include <string_view>

namespace rpg::client {

// Código do shader pelo nome do arquivo sem ".spv" ("scene.vert"); vazio se não existir.
std::span<const std::uint8_t> shaderBlob(std::string_view name);

}  // namespace rpg::client
