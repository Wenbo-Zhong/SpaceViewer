#include "view/layout_cache.hpp"
#include "view/raster.hpp"
#include "model/filter.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace {
void expect(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
bool equal(RectF a, RectF b) {
  return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}
bool equal(const std::vector<Tile>& a, const std::vector<Tile>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const auto& x = a[i]; const auto& y = b[i];
    if (x.index != y.index || x.parent != y.parent || x.size != y.size ||
        x.mergeCount != y.mergeCount || x.flags != y.flags || x.category != y.category ||
        x.hidden != y.hidden || x.container != y.container || !equal(x.outer, y.outer) ||
        !equal(x.title, y.title)) return false;
  }
  return true;
}
struct Fixture {
  std::shared_ptr<TreeSnapshot> tree = std::make_shared<TreeSnapshot>();
  TreemapLayoutCache cache;
  std::vector<uint64_t> visible;
  std::vector<uint32_t> expanded;
  ViewOptions options;
  uint64_t revision = 1;
  uint32_t root = 0;
  RectF area{10, 20, 800, 600};
  double title = 20, minW = 20, minH = 16;
  Fixture() {
    addNode(*tree, kNone, "root", kDir, CatDir, 0);
    auto folder = addNode(*tree, 0, "folder", kDir, CatDir, 0);
    addNode(*tree, folder, "a.bin", 0, CatOther, 100);
    addNode(*tree, folder, "b.jpg", 0, CatImage, 200);
    addNode(*tree, 0, "c.txt", 0, CatDoc, 400);
    tree->pending = addNode(*tree, 0, "pending", kPending, CatOther, 100);
    accountBytes(*tree, folder, 300); accountBytes(*tree, 0, 500);
    tree->freeBytes = 200;
    visible = computeVisible(*tree, Filter{});
    options.showFree = false;
    options.showUnknown = true;
  }
  const std::vector<Tile>& get(bool shouldChange) {
    bool changed = !shouldChange;
    const auto& tiles = cache.get(tree, visible, revision, root, area, title, minW, minH,
                                 expanded, options, changed);
    expect(changed == shouldChange, "cache invalidates exactly when layout inputs change");
    auto sizes = visibleForLayout(*tree, visible, options.showUnknown);
    RectF content = area;
    double freeWidth = 0;
    if (options.showFree && root == 0) {
      freeWidth = area.w * (double(tree->freeBytes) / (sizes[0] + tree->freeBytes));
      if (freeWidth < minW) freeWidth = 0;
      content.w -= freeWidth;
    }
    auto reference = layoutTreemap(*tree, sizes, root, content, title, minW, minH, expanded, options.detail);
    if (freeWidth >= minW) {
      Tile free;
      free.flags = kFree; free.category = CatFree; free.size = tree->freeBytes;
      free.outer = {area.x + content.w, area.y, freeWidth, area.h};
      reference.push_back(free);
    }
    expect(equal(tiles, reference), "cached geometry matches uncached layout");
    return tiles;
  }
};
void stableAndAppearance() {
  Fixture f;
  const Tile* data = f.get(true).data();
  for (int i = 0; i < 100; ++i) expect(f.get(false).data() == data, "stable frame retains target storage");
  f.options.fileGroups = !f.options.fileGroups;
  f.get(false);
}
void snapshotAndFilter() {
  Fixture f; f.get(true);
  const auto original = f.visible;
  f.visible[4] = 0; f.visible[0] -= 400; ++f.revision;
  f.get(true); f.get(false);
  f.visible = original; ++f.revision; f.get(true);
  f.tree = std::make_shared<TreeSnapshot>(*f.tree);
  f.tree->nodes[4].size = 900;
  f.visible[4] = 900; f.visible[0] += 500;
  f.get(true); f.get(false);
}
void viewportAndExpansion() {
  Fixture f; f.get(true);
  f.area.x += 1; f.get(true);
  f.area.y += 1; f.get(true);
  f.area.w += 1; f.get(true);
  f.area.h += 1; f.get(true);
  f.title += 1; f.get(true);
  f.minW += 1; f.get(true);
  f.minH += 1; f.get(true);
  f.options.detail = 0; f.get(true);
  f.expanded = {1}; f.get(true);
  f.root = 1; f.get(true); f.get(false);
}
void unknownAndFree() {
  Fixture f; f.get(true);
  const auto original = f.visible;
  f.options.showUnknown = false; f.get(true); f.get(false);
  expect(f.visible == original, "pending adjustment never mutates filtered sizes");
  f.area.w *= 2; f.get(true);
  f.options.showFree = true;
  expect(f.get(true).back().flags == kFree, "root includes free-space tile");
  f.root = 1;
  for (const auto& tile : f.get(true)) expect(!(tile.flags & kFree), "subdirectory excludes free-space tile");
  f.options.showUnknown = true; f.get(true);
}
void emptyAndReuse() {
  TreemapLayoutCache cache; bool changed = false;
  expect(cache.get({}, {}, 0, 0, {}, 0, 8, 8, {}, {}, changed).empty() && changed,
         "empty snapshot is handled");
  Frame frame;
  frame.resize(64, 48, 0xffffff);
  auto* data = frame.bgra.data();
  fillRect(frame, 0, 0, 64, 48, 0);
  drawText(frame, 1, 1, 40, 20, "old text", 0, 12, false);
  frame.resize(64, 48, 0x123456);
  expect(frame.bgra.data() == data && frame.texts.empty(), "frame reuse keeps pixels and clears old labels");
  expect(std::all_of(frame.bgra.begin(), frame.bgra.end(), [](uint32_t pixel) {
    return pixel == packRgb(0x123456);
  }), "reused frame clears every previous pixel");
  frame.resize(32, 16, 0xabcdef);
  expect(frame.bgra.size() == 512 && frame.bgra.back() == packRgb(0xabcdef), "resize uses current dimensions");
}
}  // namespace
int main() {
  struct Case { const char* name; void (*run)(); };
  const Case cases[] = {
    {"stableAndAppearance", stableAndAppearance}, {"snapshotAndFilter", snapshotAndFilter},
    {"viewportAndExpansion", viewportAndExpansion}, {"unknownAndFree", unknownAndFree},
    {"emptyAndReuse", emptyAndReuse}
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
