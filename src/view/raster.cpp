#include "view/raster.hpp"
#include "view/palette.hpp"

#include <algorithm>
#include <cmath>

void Frame::resize(int w, int h, uint32_t clear) {
  // 容量足够时复用像素存储，但每帧仍清底色和文字指令，避免残留上一帧内容。
  width = std::clamp(w, 1, 8192);
  height = std::clamp(h, 1, 8192);
  bgra.assign(static_cast<size_t>(width) * static_cast<size_t>(height), packRgb(clear));
  texts.clear();
}

void fillRect(Frame& frame, int x, int y, int w, int h, uint32_t rgb) {
  if (w <= 0 || h <= 0) return;
  const int x0 = std::max(0, x), y0 = std::max(0, y);
  const int x1 = std::min(frame.width, x + w), y1 = std::min(frame.height, y + h);
  if (x0 >= x1 || y0 >= y1) return;
  const uint32_t pixel = packRgb(rgb);
  for (int yy = y0; yy < y1; ++yy) {
    uint32_t* row = frame.bgra.data() + static_cast<size_t>(yy) * frame.width;
    std::fill(row + x0, row + x1, pixel);
  }
}

void fillGradient(Frame& frame, int x, int y, int w, int h, uint32_t top, uint32_t bottom) {
  if (w <= 0 || h <= 0) return;
  const int x0 = std::max(0, x), y0 = std::max(0, y);
  const int x1 = std::min(frame.width, x + w), y1 = std::min(frame.height, y + h);
  if (x0 >= x1 || y0 >= y1) return;
  const int span = std::max(1, h - 1);
  for (int yy = y0; yy < y1; ++yy) {
    const uint32_t pixel = packRgb(mixColor(top, bottom, std::clamp(yy - y, 0, span), span));
    uint32_t* row = frame.bgra.data() + static_cast<size_t>(yy) * frame.width;
    std::fill(row + x0, row + x1, pixel);
  }
}

void strokeRect(Frame& frame, int x, int y, int w, int h, uint32_t rgb, int thickness) {
  fillRect(frame, x, y, w, thickness, rgb);
  fillRect(frame, x, y + h - thickness, w, thickness, rgb);
  fillRect(frame, x, y, thickness, h, rgb);
  fillRect(frame, x + w - thickness, y, thickness, h, rgb);
}

void roundRect(Frame& frame, int x, int y, int w, int h, int radius, uint32_t color) {
  radius = std::max(0, std::min(radius, std::min(w, h) / 2));
  for (int row = 0; row < h; ++row) {
    int dy = row < radius ? radius - row - 1 : row >= h - radius ? row - (h - radius) : 0;
    int inset = dy ? radius - static_cast<int>(std::sqrt(radius * radius - dy * dy)) : 0;
    fillRect(frame, x + inset, y + row, w - inset * 2, 1, color);
  }
}

void drawText(Frame& frame, int x, int y, int w, int h, const std::string& text, uint32_t rgb,
              int fontPx, bool center) {
  if (w <= 2 || h <= 2 || text.empty()) return;
  frame.texts.push_back({x, y, w, h, fontPx, rgb, center, text});
}

void fillTilePixels(Frame& frame, const std::vector<Tile>& tiles, int x, int y, int w, int h,
                    int dividerPx, bool fileGroups) {
  if (w <= 0 || h <= 0) return;
  for (const auto& tile : tiles) {
    const int tx = static_cast<int>(std::floor(tile.outer.x));
    const int ty = static_cast<int>(std::floor(tile.outer.y));
    const int tw = static_cast<int>(std::floor(tile.outer.x + tile.outer.w)) - tx;
    const int th = static_cast<int>(std::floor(tile.outer.y + tile.outer.h)) - ty;
    if (tile.hidden || tw <= 0 || th <= 0) continue;
    const int x0 = std::max({0, x, tx}), x1 = std::min({frame.width, x + w, tx + tw});
    const int y0 = std::max({0, y, ty}), y1 = std::min({frame.height, y + h, ty + th});
    if (x0 >= x1 || y0 >= y1) continue;
    uint32_t base = categoryColor(tile.flags & kDir ? CatDir : fileGroups ? tile.category : CatOther);
    if (tile.flags & kFree) base = categoryColor(CatFree);
    if (tile.flags & kPending) base = 0xf5cba7;
    const uint32_t top = shadeColor(base, 5), bottom = shadeColor(base, -5);
    const uint32_t title = packRgb(shadeColor(base, 12)), divider = packRgb(shadeColor(base, -30));
    const int titleH = std::clamp(static_cast<int>(std::floor(tile.title.y + tile.title.h) -
                                                 std::floor(tile.title.y)), 0, th);
    const int divH = titleH > 3 ? dividerPx : 0;
    for (int yy = y0; yy < y1; ++yy) {
      const int ly = yy - ty;
      uint32_t pixel;
      if (ly < titleH) pixel = divH > 0 && ly >= titleH - divH ? divider : title;
      else pixel = packRgb(mixColor(top, bottom, ly, std::max(1, th - 1)));
      auto* row = frame.bgra.data() + static_cast<size_t>(yy) * frame.width;
      std::fill(row + x0, row + x1, pixel);
    }
  }
}
