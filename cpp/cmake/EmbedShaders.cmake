# Gera um .cpp com o SPIR-V de cada shader como arrays de bytes (cmake -P).
#   -DOUT=<arquivo.cpp> -DFILES=<a.spv;b.spv;…>
set(body "")
set(table "")
foreach(f IN LISTS FILES)
  get_filename_component(name "${f}" NAME)
  string(REGEX REPLACE "\\.spv$" "" name "${name}")
  string(MAKE_C_IDENTIFIER "${name}" id)
  file(READ "${f}" hex HEX)
  string(LENGTH "${hex}" n)
  math(EXPR bytes "${n} / 2")
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," arr "${hex}")
  string(APPEND body "alignas(4) const std::uint8_t k_${id}[${bytes}] = {${arr}};\n")
  string(APPEND table "    {\"${name}\", {k_${id}, ${bytes}}},\n")
endforeach()
file(WRITE "${OUT}.tmp" "// Gerado por cmake/EmbedShaders.cmake — não editar.
#include <cstdint>
#include <span>
#include <string_view>

#include \"client/render/ShaderBlobs.h\"

namespace rpg::client {
namespace {
${body}
struct Entry {
  std::string_view name;
  std::span<const std::uint8_t> code;
};
const Entry kShaders[] = {
${table}};
}  // namespace

std::span<const std::uint8_t> shaderBlob(std::string_view name) {
  for (const Entry& e : kShaders)
    if (e.name == name) return e.code;
  return {};
}

}  // namespace rpg::client
")
file(COPY_FILE "${OUT}.tmp" "${OUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUT}.tmp")
