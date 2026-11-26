#include "view/raster.hpp"
#include "view/palette.hpp"

#include <algorithm>
#include <cmath>

void Frame::resize(int w, int h, uint32_t clear) {
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
