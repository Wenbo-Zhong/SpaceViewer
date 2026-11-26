#include "view/raster.hpp"
#include "view/opencl_fill.hpp"
#include "view/palette.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace {
void expect(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
uint32_t pixel(const Frame& frame, int x, int y) {
  return frame.bgra[static_cast<size_t>(y) * frame.width + x];
}
Tile tile(RectF rect, uint8_t flags = 0, uint8_t category = CatOther) {
  Tile result; result.outer = rect; result.flags = flags; result.category = category;
  return result;
}
void clippingAndGradient() {
  Frame frame; frame.resize(32, 24, 0xabcdef);
  auto wide = tile({-3, -2, 40, 32});
  fillTilePixels(frame, {wide}, 5, 4, 20, 12, 1, true);
  const auto base = categoryColor(CatOther);
  expect(pixel(frame, 4, 4) == packRgb(0xabcdef) && pixel(frame, 25, 4) == packRgb(0xabcdef) &&
         pixel(frame, 5, 3) == packRgb(0xabcdef) && pixel(frame, 5, 16) == packRgb(0xabcdef),
         "fills stay inside map viewport");
  expect(pixel(frame, 5, 4) == packRgb(mixColor(shadeColor(base, 5), shadeColor(base, -5), 6, 31)),
         "clipping preserves original gradient offset and height");
  auto saved = frame.bgra;
  fillTilePixels(frame, {wide}, 0, 0, 0, 24, 1, true);
  fillTilePixels(frame, {tile({1, 1, 0, 10}), tile({1, 1, 10, 0})}, 0, 0, 32, 24, 1, true);
  expect(saved == frame.bgra, "empty map and degenerate rectangles do not alter pixels");
}
void layersAndBorders() {
  Frame frame; frame.resize(40, 30, 0xffffff);
  auto parent = tile({2, 2, 30, 24}, kDir);
  parent.title = {2, 2, 30, 6};
  auto child = tile({4, 8, 16, 12}, 0, CatDoc);
  auto hidden = tile({4, 8, 16, 12}, kFree); hidden.hidden = true;
  fillTilePixels(frame, {parent, child, hidden}, 0, 0, 40, 30, 1, true);
  expect(pixel(frame, 4, 8) == packRgb(shadeColor(categoryColor(CatDoc), 5)),
         "visible child paints over its parent; hidden tile is ignored");
  expect(pixel(frame, 8, 3) == packRgb(shadeColor(categoryColor(CatDir), 12)) &&
         pixel(frame, 8, 7) == packRgb(shadeColor(categoryColor(CatDir), -30)),
         "container title and divider retain their colors");
  strokeRect(frame, 2, 2, 30, 24, 0x112233, 1);
  expect(pixel(frame, 8, 2) == packRgb(0x112233), "borders paint above prefilled title pixels");
}
void categoriesAndSingleRow() {
  Frame frame; frame.resize(40, 30, 0xffffff);
  auto free = tile({0, 0, 10, 1}, kFree);
  auto pending = tile({10, 0, 10, 1}, kPending);
  auto image = tile({20, 0, 10, 1}, 0, CatImage);
  fillTilePixels(frame, {free, pending, image}, 0, 0, 40, 30, 1, true);
  expect(pixel(frame, 0, 0) == packRgb(shadeColor(categoryColor(CatFree), 5)) &&
         pixel(frame, 10, 0) == packRgb(shadeColor(0xf5cba7, 5)) &&
         pixel(frame, 20, 0) == packRgb(shadeColor(categoryColor(CatImage), 5)),
         "free, pending and grouped files preserve one-row gradient colors");
  fillTilePixels(frame, {image}, 0, 0, 40, 30, 1, false);
  expect(pixel(frame, 20, 0) == packRgb(shadeColor(categoryColor(CatOther), 5)),
         "grouping toggle changes only regular file colors");
}
void gpuEquivalence() {
  Frame cpu; cpu.resize(256, 192, 0xabcdef);
  Frame gpu = cpu;
  std::vector<Tile> tiles;
  for (int i = 0; i < 32; ++i) {
    auto item = tile({12.4 + i % 8 * 18.2, 10.7 + i / 8 * 24.3, 18.2, 24.3},
                     i % 3 == 0 ? kDir : 0, i % 2 ? CatDoc : CatImage);
    item.index = i;
    if (item.flags & kDir) item.title = {item.outer.x, item.outer.y, item.outer.w, 5.4};
    tiles.push_back(item);
  }
  tiles.push_back(tile({30.2, 36.7, 44.1, 40.6}, kPending));
  tiles.push_back(tile({120.4, 64.2, 45.3, 30.6}, kFree));
  auto hidden = tile({15, 15, 100, 100}, kFree); hidden.hidden = true; tiles.push_back(hidden);
  for (bool groups : {false, true}) {
    cpu.resize(256, 192, 0xabcdef); gpu = cpu;
    fillTilePixels(cpu, tiles, 16, 16, 128, 96, 1, groups);
    if (openclFillTiles(gpu, tiles, 16, 16, 128, 96, 1, groups))
      expect(cpu.bgra == gpu.bgra, "CPU batch matches OpenCL pixels for fractional, clipped, overlapping tiles");
    else
      expect(pixel(cpu, 0, 0) == packRgb(0xabcdef) && pixel(cpu, 40, 40) != packRgb(0xabcdef),
             "CPU compositor works when OpenCL is unavailable");
  }
}
}  // namespace
int main() {
  struct Case { const char* name; void (*run)(); };
  const Case cases[] = {
    {"clippingAndGradient", clippingAndGradient}, {"layersAndBorders", layersAndBorders},
    {"categoriesAndSingleRow", categoriesAndSingleRow}, {"gpuEquivalence", gpuEquivalence}
  };
  int failures = 0;
  for (const auto& item : cases) {
    try { item.run(); std::printf("case=%s PASS\n", item.name); }
    catch (const std::exception& error) {
      ++failures; std::fprintf(stderr, "case=%s FAIL %s\n", item.name, error.what());
    }
  }
  return failures ? 1 : 0;
}
