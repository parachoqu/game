#pragma once
// Serialização binária do protocolo (little-endian, independente de plataforma).
//
// Cada mensagem descreve seus campos uma vez, num `template <class A> void io(A& a)`, e o mesmo código
// serve para escrever (Writer) e ler (Reader): não há como a leitura sair de passo com a escrita.
//
//   a(x)          — campo de tipo básico, enum, string, vector, optional, array ou struct com io()
//   a.f32(x)      — double enviado como float (posições e ângulos no fio; a simulação usa double)
//   a.quant(x, s) — double como inteiro de 16 bits na escala `s` (frações, progresso)
//
// O Reader nunca lê além do buffer: ao faltar byte, marca erro e devolve zeros. Quem recebe confere
// `ok()` e descarta a mensagem inteira (entrada da rede não é confiável).
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace rpg::protocol {

using Bytes = std::vector<std::uint8_t>;

class Writer {
 public:
  static constexpr bool kReading = false;

  const Bytes& bytes() const { return buf_; }
  Bytes take() { return std::move(buf_); }

  void u8(std::uint8_t v) { buf_.push_back(v); }
  void u16(std::uint16_t v) { putLE(v); }
  void u32(std::uint32_t v) { putLE(v); }
  void u64(std::uint64_t v) { putLE(v); }
  void f64(double v) { putLE(std::bit_cast<std::uint64_t>(v)); }
  void f32(const double& v) { putLE(std::bit_cast<std::uint32_t>(static_cast<float>(v))); }
  void quant(const double& v, double scale) {
    const double q = std::round(v * scale);
    const double c = q < -32768.0 ? -32768.0 : q > 32767.0 ? 32767.0 : q;
    putLE(static_cast<std::uint16_t>(static_cast<std::int16_t>(c)));
  }
  // Inteiro sem sinal de tamanho variável (7 bits por byte).
  void varint(std::uint64_t v) {
    while (v >= 0x80) {
      buf_.push_back(static_cast<std::uint8_t>(v | 0x80));
      v >>= 7;
    }
    buf_.push_back(static_cast<std::uint8_t>(v));
  }

  template <class T>
  void operator()(const T& v) {
    write(v);
  }

 private:
  template <class U>
  void putLE(U v) {
    for (std::size_t i = 0; i < sizeof(U); ++i) buf_.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
  }

  template <class T>
  void write(const T& v) {
    if constexpr (std::is_same_v<T, bool>) {
      u8(v ? 1 : 0);
    } else if constexpr (std::is_enum_v<T>) {
      write(static_cast<std::underlying_type_t<T>>(v));
    } else if constexpr (std::is_same_v<T, double>) {
      f64(v);
    } else if constexpr (std::is_same_v<T, float>) {
      putLE(std::bit_cast<std::uint32_t>(v));
    } else if constexpr (std::is_integral_v<T>) {
      putLE(static_cast<std::make_unsigned_t<T>>(v));
    } else if constexpr (std::is_same_v<T, std::string>) {
      varint(v.size());
      buf_.insert(buf_.end(), v.begin(), v.end());
    } else {
      writeCompound(v);
    }
  }

  template <class T>
  void writeCompound(const std::vector<T>& v) {
    varint(v.size());
    for (const T& x : v) write(x);
  }
  template <class T, std::size_t N>
  void writeCompound(const std::array<T, N>& v) {
    for (const T& x : v) write(x);
  }
  template <class T>
  void writeCompound(const std::optional<T>& v) {
    u8(v ? 1 : 0);
    if (v) write(*v);
  }
  template <class T>
  void writeCompound(const std::set<T>& v) {
    varint(v.size());
    for (const T& x : v) write(x);
  }
  template <class K, class V>
  void writeCompound(const std::map<K, V>& m) {
    varint(m.size());
    for (const auto& [k, v] : m) {
      write(k);
      write(v);
    }
  }
  template <class A, class B>
  void writeCompound(const std::pair<A, B>& p) {
    write(p.first);
    write(p.second);
  }
  template <class... Ts>
  void writeCompound(const std::variant<Ts...>& v) {
    u8(static_cast<std::uint8_t>(v.index()));
    std::visit([&w = *this](const auto& x) { w(x); }, v);
  }
  template <class T>
  void writeCompound(const T& v) {
    const_cast<T&>(v).io(*this);  // io() só lê os campos quando o arquivo é Writer
  }

  Bytes buf_;
};

class Reader {
 public:
  static constexpr bool kReading = true;

  explicit Reader(std::span<const std::uint8_t> data) : data_(data) {}

  bool ok() const { return ok_; }
  bool atEnd() const { return pos_ == data_.size(); }
  void fail() { ok_ = false; }

  std::uint8_t u8() {
    std::uint8_t v = 0;
    getLE(v);
    return v;
  }
  void f32(double& v) {
    std::uint32_t u = 0;
    getLE(u);
    v = static_cast<double>(std::bit_cast<float>(u));
    if (!std::isfinite(v)) {  // entrada da rede: NaN e ∞ não entram na simulação
      v = 0;
      ok_ = false;
    }
  }
  void quant(double& v, double scale) {
    std::uint16_t u = 0;
    getLE(u);
    v = static_cast<double>(static_cast<std::int16_t>(u)) / scale;
  }
  std::uint64_t varint() {
    std::uint64_t v = 0;
    for (int shift = 0; shift < 64; shift += 7) {
      const std::uint8_t b = u8();
      if (!ok_) return 0;
      v |= static_cast<std::uint64_t>(b & 0x7F) << shift;
      if (!(b & 0x80)) return v;
    }
    ok_ = false;
    return 0;
  }

  template <class T>
  void operator()(T& v) {
    read(v);
  }

 private:
  static constexpr std::size_t kMaxCount = 1u << 20;  // nenhuma lista legítima passa disso

  template <class U>
  void getLE(U& v) {
    if (!ok_ || data_.size() - pos_ < sizeof(U)) {
      ok_ = false;
      v = 0;
      return;
    }
    U r = 0;
    for (std::size_t i = 0; i < sizeof(U); ++i) r |= static_cast<U>(static_cast<U>(data_[pos_ + i]) << (8 * i));
    pos_ += sizeof(U);
    v = r;
  }

  std::size_t count() {
    const std::uint64_t n = varint();
    if (n > kMaxCount || n > data_.size() - pos_ + 1) {  // cada elemento ocupa ao menos um byte
      ok_ = false;
      return 0;
    }
    return static_cast<std::size_t>(n);
  }

  template <class T>
  void read(T& v) {
    if constexpr (std::is_same_v<T, bool>) {
      v = u8() != 0;
    } else if constexpr (std::is_enum_v<T>) {
      std::underlying_type_t<T> u{};
      read(u);
      v = static_cast<T>(u);
    } else if constexpr (std::is_same_v<T, double>) {
      std::uint64_t u = 0;
      getLE(u);
      v = std::bit_cast<double>(u);
    } else if constexpr (std::is_same_v<T, float>) {
      std::uint32_t u = 0;
      getLE(u);
      v = std::bit_cast<float>(u);
    } else if constexpr (std::is_integral_v<T>) {
      std::make_unsigned_t<T> u = 0;
      getLE(u);
      v = static_cast<T>(u);
    } else if constexpr (std::is_same_v<T, std::string>) {
      const std::size_t n = count();
      if (!ok_) return;
      v.assign(reinterpret_cast<const char*>(data_.data() + pos_), n);
      pos_ += n;
    } else {
      readCompound(v);
    }
  }

  template <class T>
  void readCompound(std::vector<T>& v) {
    const std::size_t n = count();
    v.clear();
    v.reserve(n);
    for (std::size_t i = 0; i < n && ok_; ++i) {
      T x{};
      read(x);
      v.push_back(std::move(x));
    }
  }
  template <class T, std::size_t N>
  void readCompound(std::array<T, N>& v) {
    for (T& x : v) read(x);
  }
  template <class T>
  void readCompound(std::optional<T>& v) {
    if (u8()) {
      T x{};
      read(x);
      v = std::move(x);
    } else {
      v.reset();
    }
  }
  template <class T>
  void readCompound(std::set<T>& v) {
    const std::size_t n = count();
    v.clear();
    for (std::size_t i = 0; i < n && ok_; ++i) {
      T x{};
      read(x);
      v.insert(std::move(x));
    }
  }
  template <class K, class V>
  void readCompound(std::map<K, V>& m) {
    const std::size_t n = count();
    m.clear();
    for (std::size_t i = 0; i < n && ok_; ++i) {
      K k{};
      V x{};
      read(k);
      read(x);
      m.emplace(std::move(k), std::move(x));
    }
  }
  template <class A, class B>
  void readCompound(std::pair<A, B>& p) {
    read(p.first);
    read(p.second);
  }
  template <class... Ts>
  void readCompound(std::variant<Ts...>& v) {
    const std::uint8_t index = u8();
    if (index >= sizeof...(Ts)) {
      ok_ = false;
      return;
    }
    emplaceIndex<0, Ts...>(v, index);
  }
  template <std::size_t I, class First, class... Rest, class V>
  void emplaceIndex(V& v, std::uint8_t index) {
    if (index == I) {
      First x{};
      read(x);
      v = std::move(x);
    } else if constexpr (sizeof...(Rest) > 0) {
      emplaceIndex<I + 1, Rest...>(v, index);
    }
  }
  template <class T>
  void readCompound(T& v) {
    v.io(*this);
  }

  std::span<const std::uint8_t> data_;
  std::size_t pos_ = 0;
  bool ok_ = true;
};

// Serializa qualquer valor com io().
template <class T>
Bytes encode(const T& v) {
  Writer w;
  w(v);
  return w.take();
}

// Lê um valor completo; nullopt se faltar byte, sobrar byte ou algum campo for inválido.
template <class T>
std::optional<T> decode(std::span<const std::uint8_t> bytes) {
  Reader r(bytes);
  T v{};
  r(v);
  if (!r.ok() || !r.atEnd()) return std::nullopt;
  return v;
}

}  // namespace rpg::protocol
