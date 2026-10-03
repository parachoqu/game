#include "core/BinaryReader.h"

#include <bit>
#include <fstream>
#include <iterator>

#include "core/Paths.h"
#include "core/data/DataError.h"

namespace rpg {

std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw DataError("não foi possível abrir " + pathToUtf8(path));
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void ByteCursor::need(std::size_t n) const {
  if (bytes_.size() - at_ < n) throw DataError(name_ + ": arquivo termina antes do esperado");
}

std::uint8_t ByteCursor::u8() {
  need(1);
  return bytes_[at_++];
}

std::uint16_t ByteCursor::u16() {
  need(2);
  const auto v = static_cast<std::uint16_t>(bytes_[at_] | (bytes_[at_ + 1] << 8));
  at_ += 2;
  return v;
}

std::uint32_t ByteCursor::u32() {
  need(4);
  std::uint32_t v = 0;
  for (int i = 3; i >= 0; --i) v = (v << 8) | bytes_[at_ + static_cast<std::size_t>(i)];
  at_ += 4;
  return v;
}

float ByteCursor::f32() { return std::bit_cast<float>(u32()); }

double ByteCursor::f64() {
  need(8);
  std::uint64_t v = 0;
  for (int i = 7; i >= 0; --i) v = (v << 8) | bytes_[at_ + static_cast<std::size_t>(i)];
  at_ += 8;
  return std::bit_cast<double>(v);
}

std::string ByteCursor::ascii(std::size_t n) {
  need(n);
  std::string s(bytes_.begin() + static_cast<std::ptrdiff_t>(at_), bytes_.begin() + static_cast<std::ptrdiff_t>(at_ + n));
  at_ += n;
  return s;
}

namespace {

template <class T, class Read>
std::vector<T> readArray(const std::filesystem::path& path, std::size_t count, std::size_t elemSize, Read read) {
  const auto bytes = readFileBytes(path);
  if (bytes.size() != count * elemSize) {
    throw DataError(pathToUtf8(path.filename()) + ": " + std::to_string(bytes.size()) + " bytes, esperado " +
                    std::to_string(count * elemSize));
  }
  ByteCursor c(bytes, pathToUtf8(path.filename()));
  std::vector<T> out;
  out.reserve(count);
  for (std::size_t i = 0; i < count; ++i) out.push_back(read(c));
  return out;
}

}  // namespace

std::vector<std::uint16_t> readU16File(const std::filesystem::path& path, std::size_t expectedCount) {
  return readArray<std::uint16_t>(path, expectedCount, 2, [](ByteCursor& c) { return c.u16(); });
}

std::vector<float> readF32File(const std::filesystem::path& path, std::size_t expectedCount) {
  return readArray<float>(path, expectedCount, 4, [](ByteCursor& c) { return c.f32(); });
}

std::vector<std::uint8_t> readU8File(const std::filesystem::path& path, std::size_t expectedCount) {
  auto bytes = readFileBytes(path);
  if (bytes.size() != expectedCount) {
    throw DataError(pathToUtf8(path.filename()) + ": " + std::to_string(bytes.size()) + " bytes, esperado " +
                    std::to_string(expectedCount));
  }
  return bytes;
}

}  // namespace rpg
