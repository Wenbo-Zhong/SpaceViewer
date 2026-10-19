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
               double titlePx, double minW, double minH, int depth, const std::vector<uint32_t>& expanded, std::vector<Tile>& tiles);

void emitChildren(const TreeSnapshot& tree, const std::vector<uint64_t>& visible, uint32_t parent,
                  const std::vector<Item>& items, RectF area, double titlePx, double minW, double minH,
                  int depth, const std::vector<uint32_t>& expanded, std::vector<Tile>& tiles) {
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
        const bool explicitExpand = std::find(expanded.begin(), expanded.end(), item.index) != expanded.end();
        const bool nest = (depth < 2 || explicitExpand) && item.mergeCount == 0 && (item.flags & kDir) && item.index != kNone &&
                          (tree.nodes[item.index].childCount > 1 || explicitExpand) &&
                          outer.w >= (explicitExpand ? minW * 2 : std::max(100.0, minW * 4)) &&
                          outer.h >= titlePx + (explicitExpand ? minH + 4 : std::max(64.0, minH * 4));
        tile.container = nest;
        double titleH = nest ? titlePx : 0;
        tile.title = RectF{outer.x, outer.y, outer.w, titleH};
        tiles.push_back(tile);
        if (nest) {
          RectF inner{outer.x + 3, outer.y + titleH, outer.w - 6, outer.h - titleH - 3};
          if (inner.w >= minW && inner.h >= minH) layoutDir(tree, visible, item.index, inner, titlePx, minW, minH, depth + 1, expanded, tiles);
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
        const bool explicitExpand = std::find(expanded.begin(), expanded.end(), item.index) != expanded.end();
        const bool nest = (depth < 2 || explicitExpand) && item.mergeCount == 0 && (item.flags & kDir) && item.index != kNone &&
                          (tree.nodes[item.index].childCount > 1 || explicitExpand) &&
                          outer.w >= (explicitExpand ? minW * 2 : std::max(100.0, minW * 4)) &&
                          outer.h >= titlePx + (explicitExpand ? minH + 4 : std::max(64.0, minH * 4));
        tile.container = nest;
        double titleH = nest ? titlePx : 0;
        tile.title = RectF{outer.x, outer.y, outer.w, titleH};
        tiles.push_back(tile);
        if (nest) {
          RectF inner{outer.x + 3, outer.y + titleH, outer.w - 6, outer.h - titleH - 3};
          if (inner.w >= minW && inner.h >= minH) layoutDir(tree, visible, item.index, inner, titlePx, minW, minH, depth + 1, expanded, tiles);
        }
        x += width;
      }
      box.y += height;
      box.h -= height;
    }
  };

  while (cursor < n && box.w > 1e-9 && box.h > 1e-9) {
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

struct Child {
  uint32_t index;
  uint64_t size;
};

void layoutDir(const TreeSnapshot& tree, const std::vector<uint64_t>& visible, uint32_t dir, RectF area,
               double titlePx, double minW, double minH, int depth, const std::vector<uint32_t>& expanded, std::vector<Tile>& tiles) {
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
  // Keep the complete byte total: omitted entries retain their proportional area.
  uint64_t total = 0;
  for (const Child& child : children) total += child.size;
  if (!total) return;
  std::vector<Item> items;
  Item remainder;
  remainder.index = kNone;
  for (const Child& child : children) {
    const double childArea = pixels * (static_cast<double>(child.size) / total);
    if (childArea < minW * minH || items.size() >= 800) {
      remainder.size += child.size;
      ++remainder.mergeCount;
      continue;
    }
    Item item;
    item.index = child.index;
    item.size = child.size;
    item.flags = tree.nodes[child.index].flags;
    item.category = tree.nodes[child.index].category;
    item.area = childArea;
    items.push_back(item);
  }
  if (remainder.mergeCount) {
    remainder.area = pixels * (static_cast<double>(remainder.size) / total);
    items.push_back(remainder);
  }
  std::stable_sort(items.begin(), items.end(),
                   [](const Item& a, const Item& b) { return a.size > b.size; });
  emitChildren(tree, visible, dir, items, area, titlePx, minW, minH, depth, expanded, tiles);
}

}  // namespace

std::vector<Tile> layoutTreemap(const TreeSnapshot& tree, const std::vector<uint64_t>& visible, uint32_t root,
                                const RectF& area, double titlePx, double minW, double minH,
                                const std::vector<uint32_t>& expanded) {
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
  layoutDir(tree, visible, root, area, titlePx, minW, minH, 0, expanded, tiles);
  return tiles;
}

std::vector<uint32_t> expansionPath(const TreeSnapshot& tree, uint32_t root, uint32_t selected) {
  std::vector<uint32_t> path;
  uint32_t index = selected;
  while (index != root && index != kNone && index < tree.nodes.size()) {
    if (!(tree.nodes[index].flags & kDir)) return {};
    path.push_back(index);
    index = tree.nodes[index].parent;
    if (path.size() > tree.nodes.size()) return {};
  }
  if (index != root) return {};
  std::reverse(path.begin(), path.end());
  return path;
}
