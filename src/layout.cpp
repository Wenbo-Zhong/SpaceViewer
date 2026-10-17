#include "layout.hpp"

#include "util.hpp"

#include <algorithm>
#include <cmath>

namespace {

struct Item {
  uint32_t index = kNone;
  double area = 0;
  uint64_t size = 0;
  uint32_t mergeCount = 0;
  uint8_t flags = 0;
  uint8_t category = CatOther;
};

double worst(const std::vector<double>& row, double side) {
  if (row.empty() || side <= 0) return 1e300;
  double sum = 0;
  double mn = row[0];
  double mx = row[0];
  for (double area : row) {
    sum += area;
    if (area < mn) mn = area;
    if (area > mx) mx = area;
  }
  if (sum <= 0 || mn <= 0) return 1e300;
  double side2 = side * side;
  double sum2 = sum * sum;
  return std::max(side2 * mx / sum2, sum2 / (side2 * mn));
}

void layoutDir(const TreeSnapshot& tree, const std::vector<uint64_t>& visible, uint32_t dir, RectF area,
               double titlePx, double minW, double minH, std::vector<Tile>& tiles);

void emitChildren(const TreeSnapshot& tree, const std::vector<uint64_t>& visible, uint32_t parent,
                  const std::vector<Item>& items, RectF area, double titlePx, double minW, double minH,
                  bool recurse, std::vector<Tile>& tiles) {
  if (items.empty() || area.w <= 1 || area.h <= 1) return;
  const size_t n = items.size();
  size_t cursor = 0;
  std::vector<int> row;
  row.reserve(32);
  RectF box = area;
  auto emitRow = [&](const std::vector<int>& which) {
    double sum = 0;
    for (int index : which) sum += items[static_cast<size_t>(index)].area;
    if (sum <= 0 || which.empty()) return;
    const bool horizontal = box.w >= box.h;
    if (horizontal) {
      double width = box.h > 0 ? sum / box.h : 0;
      if (width > box.w) width = box.w;
      double y = box.y;
      for (size_t k = 0; k < which.size(); ++k) {
        const Item& item = items[static_cast<size_t>(which[k])];
        double height = width > 0 ? item.area / width : 0;
        if (k + 1 == which.size()) height = (box.y + box.h) - y;
        if (height < 0) height = 0;
        RectF outer{box.x, y, width, height};
        Tile tile;
        tile.index = item.index;
        tile.parent = parent;
        tile.size = item.size;
        tile.mergeCount = item.mergeCount;
        tile.flags = item.flags;
        tile.category = item.category;
        tile.outer = outer;
        const bool nest = recurse && item.mergeCount == 0 && (item.flags & kDir) && item.index != kNone &&
                          outer.w >= minW * 2 && outer.h >= titlePx + minH + 4;
        double titleH = nest ? titlePx : 0;
        tile.title = RectF{outer.x, outer.y, outer.w, titleH};
        tiles.push_back(tile);
        if (nest) {
          RectF inner{outer.x + 1, outer.y + titleH, outer.w - 2, outer.h - titleH - 1};
          if (inner.w >= minW && inner.h >= minH) layoutDir(tree, visible, item.index, inner, titlePx, minW, minH, tiles);
        }
        y += height;
      }
      box.x += width;
      box.w -= width;
    } else {
      double height = box.w > 0 ? sum / box.w : 0;
      if (height > box.h) height = box.h;
      double x = box.x;
      for (size_t k = 0; k < which.size(); ++k) {
        const Item& item = items[static_cast<size_t>(which[k])];
        double width = height > 0 ? item.area / height : 0;
        if (k + 1 == which.size()) width = (box.x + box.w) - x;
        if (width < 0) width = 0;
        RectF outer{x, box.y, width, height};
        Tile tile;
        tile.index = item.index;
        tile.parent = parent;
        tile.size = item.size;
        tile.mergeCount = item.mergeCount;
        tile.flags = item.flags;
        tile.category = item.category;
        tile.outer = outer;
        const bool nest = recurse && item.mergeCount == 0 && (item.flags & kDir) && item.index != kNone &&
                          outer.w >= minW * 2 && outer.h >= titlePx + minH + 4;
        double titleH = nest ? titlePx : 0;
        tile.title = RectF{outer.x, outer.y, outer.w, titleH};
        tiles.push_back(tile);
        if (nest) {
          RectF inner{outer.x + 1, outer.y + titleH, outer.w - 2, outer.h - titleH - 1};
          if (inner.w >= minW && inner.h >= minH) layoutDir(tree, visible, item.index, inner, titlePx, minW, minH, tiles);
        }
        x += width;
      }
      box.y += height;
      box.h -= height;
    }
  };

  while (cursor < n && box.w > 0.5 && box.h > 0.5) {
    std::vector<double> withNext;
    withNext.reserve(row.size() + 1);
    for (int index : row) withNext.push_back(items[static_cast<size_t>(index)].area);
    withNext.push_back(items[cursor].area);
    std::vector<double> current;
    current.reserve(row.size());
    for (int index : row) current.push_back(items[static_cast<size_t>(index)].area);
    double side = std::min(box.w, box.h);
    if (row.empty() || worst(withNext, side) <= worst(current, side)) {
      row.push_back(static_cast<int>(cursor));
      ++cursor;
    } else {
      emitRow(row);
      row.clear();
    }
  }
  if (!row.empty()) emitRow(row);
}

bool blocksFit(const std::vector<Tile>& tiles, uint32_t dir, double minW, double minH) {
  bool any = false;
  for (const Tile& tile : tiles) {
    if (tile.parent != dir) continue;
    any = true;
    if (tile.outer.w + 0.5 < minW || tile.outer.h + 0.5 < minH) return false;
  }
  return any;
}

struct Child {
  uint32_t index;
  uint64_t size;
};

std::vector<Item> takeLargest(const TreeSnapshot& tree, const std::vector<Child>& children, int count, double pixels) {
  std::vector<Item> items;
  uint64_t sum = 0;
  int n = std::min(count, static_cast<int>(children.size()));
  for (int i = 0; i < n; ++i) sum += children[static_cast<size_t>(i)].size;
  if (sum == 0) return items;
  items.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    const Child& child = children[static_cast<size_t>(i)];
    Item item;
    item.index = child.index;
    item.size = child.size;
    item.flags = tree.nodes[child.index].flags;
    item.category = tree.nodes[child.index].category;
    item.area = pixels * (static_cast<double>(child.size) / static_cast<double>(sum));
    items.push_back(item);
  }
  return items;
}

void layoutDir(const TreeSnapshot& tree, const std::vector<uint64_t>& visible, uint32_t dir, RectF area,
               double titlePx, double minW, double minH, std::vector<Tile>& tiles) {
  if (dir >= tree.nodes.size() || area.w <= 1 || area.h <= 1) return;
  if (minW < 8) minW = 8;
  if (minH < 8) minH = 8;
  std::vector<Child> children;
  for (uint32_t c = tree.nodes[dir].firstChild; c != kNone && c < tree.nodes.size();
       c = tree.nodes[c].nextSibling) {
    if (c >= visible.size() || visible[c] == 0) continue;
    children.push_back(Child{c, visible[c]});
  }
  if (children.empty()) return;
  std::sort(children.begin(), children.end(),
            [](const Child& a, const Child& b) { return a.size > b.size; });

  const double pixels = std::max(1.0, area.w * area.h);
  int maxK = static_cast<int>(children.size());
  int byArea = static_cast<int>(pixels / (minW * minH));
  if (byArea < 1) byArea = 1;
  if (maxK > byArea) maxK = byArea;
  if (maxK > 800) maxK = 800;

  auto acceptable = [&](int count) {
    std::vector<Tile> trial;
    std::vector<Item> items = takeLargest(tree, children, count, pixels);
    emitChildren(tree, visible, dir, items, area, titlePx, minW, minH, false, trial);
    return blocksFit(trial, dir, minW, minH);
  };

  int best = 1;
  if (acceptable(1)) {
    int lo = 1;
    int hi = maxK;
    while (lo <= hi) {
      int mid = (lo + hi) / 2;
      if (acceptable(mid)) {
        best = mid;
        lo = mid + 1;
      } else {
        hi = mid - 1;
      }
    }
  }
  std::vector<Item> items = takeLargest(tree, children, best, pixels);
  emitChildren(tree, visible, dir, items, area, titlePx, minW, minH, true, tiles);
}

}  // namespace

std::vector<Tile> layoutTreemap(const TreeSnapshot& tree, const std::vector<uint64_t>& visible, uint32_t root,
                                const RectF& area, double titlePx, double minW, double minH) {
  std::vector<Tile> tiles;
  if (root >= tree.nodes.size()) return tiles;
  if ((tree.nodes[root].flags & kDir) == 0) {
    Tile tile;
    tile.index = root;
    tile.size = root < visible.size() ? visible[root] : tree.nodes[root].size;
    tile.flags = tree.nodes[root].flags;
    tile.category = tree.nodes[root].category;
    tile.outer = area;
    tiles.push_back(tile);
    return tiles;
  }
  layoutDir(tree, visible, root, area, titlePx, minW, minH, tiles);
  return tiles;
}
