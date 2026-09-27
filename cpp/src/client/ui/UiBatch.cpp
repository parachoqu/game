#include "client/ui/UiBatch.h"

#include <cmath>

namespace rpg::client {

Rgba Rgba::css(std::string_view s, float alpha) {
  if (!s.empty() && s[0] == '#') s.remove_prefix(1);
  std::uint32_t v = 0;
  for (char ch : s.substr(0, 6)) {
    v <<= 4;
    if (ch >= '0' && ch <= '9') v |= static_cast<std::uint32_t>(ch - '0');
    else if (ch >= 'a' && ch <= 'f') v |= static_cast<std::uint32_t>(ch - 'a' + 10);
    else if (ch >= 'A' && ch <= 'F') v |= static_cast<std::uint32_t>(ch - 'A' + 10);
  }
  return hex(v, alpha);
}

void UiBatch::quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Rgba c) {
  const std::uint32_t k = c.packed();
  verts_.push_back({x0, y0, u0, v0, k});
  verts_.push_back({x1, y0, u1, v0, k});
  verts_.push_back({x1, y1, u1, v1, k});
  verts_.push_back({x0, y0, u0, v0, k});
  verts_.push_back({x1, y1, u1, v1, k});
  verts_.push_back({x0, y1, u0, v1, k});
}

void UiBatch::rect(float x, float y, float w, float h, Rgba c) {
  if (w <= 0 || h <= 0 || c.a == 0) return;
  const float u = atlas_->whiteU(), v = atlas_->whiteV();
  quad(x, y, x + w, y + h, u * 0.5f, v * 0.5f, u * 0.5f, v * 0.5f, c);
}

void UiBatch::frame(float x, float y, float w, float h, float t, Rgba c) {
  rect(x, y, w, t, c);
  rect(x, y + h - t, w, t, c);
  rect(x, y + t, t, h - 2 * t, c);
  rect(x + w - t, y + t, t, h - 2 * t, c);
}

float UiBatch::measure(int face, int px, std::string_view s) {
  float w = 0;
  char32_t prev = 0;
  for (std::size_t i = 0; i < s.size();) {
    const char32_t cp = nextCodepoint(s, i);
    if (prev) w += atlas_->kerning(face, px, prev, cp);
    w += atlas_->glyph(face, px, cp).advance;
    prev = cp;
  }
  return w;
}

float UiBatch::text(int face, int px, float x, float y, std::string_view s, Rgba c, Align align) {
  const float w = measure(face, px, s);
  if (align == Align::Center) x -= w * 0.5f;
  else if (align == Align::Right) x -= w;
  x = std::round(x);
  const float base = std::round(y + atlas_->ascender(face, px));
  char32_t prev = 0;
  float pen = x;
  for (std::size_t i = 0; i < s.size();) {
    const char32_t cp = nextCodepoint(s, i);
    if (prev) pen += atlas_->kerning(face, px, prev, cp);
    const Glyph& g = atlas_->glyph(face, px, cp);
    if (g.w > 0) {
      const float gx = std::round(pen) + static_cast<float>(g.bearingX), gy = base - static_cast<float>(g.bearingY);
      quad(gx, gy, gx + static_cast<float>(g.w), gy + static_cast<float>(g.h), g.u0, g.v0, g.u1, g.v1, c);
    }
    pen += g.advance;
    prev = cp;
  }
  return w;
}

float UiBatch::textShadow(int face, int px, float x, float y, std::string_view s, Rgba c, Align align) {
  text(face, px, x + 1, y + 1, s, Rgba{0, 0, 0, static_cast<std::uint8_t>(c.a * 0.8f)}, align);
  return text(face, px, x, y, s, c, align);
}

std::vector<std::string> UiBatch::wrap(int face, int px, float maxW, std::string_view s) {
  std::vector<std::string> lines;
  std::string line;
  std::size_t i = 0;
  while (i <= s.size()) {
    std::size_t j = s.find(' ', i);
    if (j == std::string_view::npos) j = s.size();
    const std::string_view word = s.substr(i, j - i);
    const std::string candidate = line.empty() ? std::string(word) : line + " " + std::string(word);
    if (!line.empty() && measure(face, px, candidate) > maxW) {
      lines.push_back(line);
      line = std::string(word);
    } else {
      line = candidate;
    }
    i = j + 1;
  }
  if (!line.empty()) lines.push_back(line);
  return lines;
}

float UiBatch::paragraph(int face, int px, float x, float y, float maxW, std::string_view s, Rgba c, float lineGap) {
  const float lh = static_cast<float>(px) * lineGap;
  float yy = y;
  for (const std::string& l : wrap(face, px, maxW, s)) {
    text(face, px, x, yy, l, c);
    yy += lh;
  }
  return yy - y;
}

}  // namespace rpg::client
