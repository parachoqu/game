#pragma once
// Leitura de arquivos binários little-endian (heightfields, máscaras e colisores do pacote de
// simulação). Funciona igual em qualquer arquitetura: os bytes são montados explicitamente.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rpg {

// Conteúdo inteiro do arquivo. DataError se não abrir.
std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path);

// Cursor sobre um buffer; ler além do fim lança DataError com o nome do arquivo.
class ByteCursor {
 public:
  ByteCursor(const std::vector<std::uint8_t>& bytes, std::string name) : bytes_(bytes), name_(std::move(name)) {}

  std::uint8_t u8();
  std::uint16_t u16();
  std::uint32_t u32();
  float f32();
  double f64();
  std::string ascii(std::size_t n);

  std::size_t offset() const { return at_; }
  std::size_t remaining() const { return bytes_.size() - at_; }

 private:
  void need(std::size_t n) const;
  const std::vector<std::uint8_t>& bytes_;
  std::string name_;
  std::size_t at_ = 0;
};

// Arquivo inteiro como vetor de u16 / f32 little-endian, conferindo a quantidade de elementos.
std::vector<std::uint16_t> readU16File(const std::filesystem::path& path, std::size_t expectedCount);
std::vector<float> readF32File(const std::filesystem::path& path, std::size_t expectedCount);
std::vector<std::uint8_t> readU8File(const std::filesystem::path& path, std::size_t expectedCount);

}  // namespace rpg
