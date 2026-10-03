#include "client/image/PngWriter.h"

#include <algorithm>
#include <array>
#include <fstream>

namespace rpg::client {

namespace {

std::uint32_t crc32(const std::uint8_t* d, std::size_t n, std::uint32_t c = 0) {
  static const auto table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t v = i;
      for (int k = 0; k < 8; ++k) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
      t[i] = v;
    }
    return t;
  }();
  c = ~c;
  for (std::size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
  return ~c;
}

void be32(std::vector<std::uint8_t>& o, std::uint32_t v) {
  o.push_back(static_cast<std::uint8_t>(v >> 24));
  o.push_back(static_cast<std::uint8_t>(v >> 16));
  o.push_back(static_cast<std::uint8_t>(v >> 8));
  o.push_back(static_cast<std::uint8_t>(v));
}

void chunk(std::vector<std::uint8_t>& o, const char* type, const std::vector<std::uint8_t>& data) {
  be32(o, static_cast<std::uint32_t>(data.size()));
  const std::size_t start = o.size();
  o.insert(o.end(), type, type + 4);
  o.insert(o.end(), data.begin(), data.end());
  be32(o, crc32(o.data() + start, o.size() - start));
}

}  // namespace

std::vector<std::uint8_t> encodePng(std::span<const std::uint8_t> rgba, int w, int h) {
  // linhas com filtro 0
  std::vector<std::uint8_t> raw;
  raw.reserve(static_cast<std::size_t>(h) * (static_cast<std::size_t>(w) * 4 + 1));
  for (int y = 0; y < h; ++y) {
    raw.push_back(0);
    const auto* row = rgba.data() + static_cast<std::size_t>(y) * w * 4;
    raw.insert(raw.end(), row, row + static_cast<std::size_t>(w) * 4);
  }
  // zlib: cabeçalho, blocos "stored" de até 65535 bytes, adler32
  std::vector<std::uint8_t> z = {0x78, 0x01};
  std::uint32_t a = 1, b = 0;
  for (std::uint8_t v : raw) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  std::size_t pos = 0;
  do {
    const std::size_t n = std::min<std::size_t>(65535, raw.size() - pos);
    const bool last = pos + n == raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back(static_cast<std::uint8_t>(n));
    z.push_back(static_cast<std::uint8_t>(n >> 8));
    z.push_back(static_cast<std::uint8_t>(~n));
    z.push_back(static_cast<std::uint8_t>(~n >> 8));
    z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
    pos += n;
  } while (pos < raw.size());
  be32(z, (b << 16) | a);

  std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<std::uint8_t> ihdr;
  be32(ihdr, static_cast<std::uint32_t>(w));
  be32(ihdr, static_cast<std::uint32_t>(h));
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8 bits, RGBA
  chunk(out, "IHDR", ihdr);
  chunk(out, "IDAT", z);
  chunk(out, "IEND", {});
  return out;
}

bool writePng(const std::filesystem::path& file, std::span<const std::uint8_t> rgba, int w, int h) {
  const auto bytes = encodePng(rgba, w, h);
  std::ofstream f(file, std::ios::binary);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(f);
}

}  // namespace rpg::client
