#include "view/palette.hpp"
#include "model/category.hpp"

uint32_t categoryColor(uint8_t category) {
  switch (category) {
    case CatDir: return 0xc7a580;
    case CatImage: return 0x7dcea0;
    case CatVideo: return 0xc39bd3;
    case CatAudio: return 0xf5cba7;
    case CatArchive: return 0xf7dc6f;
    case CatDoc: return 0x85c1e9;
    case CatCode: return 0x76d7c4;
    case CatExe: return 0xf1948a;
    case CatFree: return 0xe5e8eb;
    default: return 0x91b3cf;
  }
}

uint32_t shadeColor(uint32_t rgb, int delta) {
  auto ch = [delta](int c) {
    int v = c + delta;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return v;
  };
  int r = ch((rgb >> 16) & 255);
  int g = ch((rgb >> 8) & 255);
  int b = ch(rgb & 255);
  return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

uint32_t mixColor(uint32_t a, uint32_t b, int tNum, int tDen) {
  if (tDen <= 0) return a;
  auto ch = [&](int shift) {
    int ca = (a >> shift) & 255;
    int cb = (b >> shift) & 255;
    return ca + (cb - ca) * tNum / tDen;
  };
  return (static_cast<uint32_t>(ch(16)) << 16) | (static_cast<uint32_t>(ch(8)) << 8) |
         static_cast<uint32_t>(ch(0));
}
