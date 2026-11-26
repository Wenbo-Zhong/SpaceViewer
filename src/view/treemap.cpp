#include "app/state.hpp"
#include "view/layout.hpp"
#include "view/metrics.hpp"
#include "view/opencl_fill.hpp"
#include "view/palette.hpp"
#include "view/raster.hpp"
#include "i18n/format.hpp"
#include "i18n/text.hpp"
#include "i18n/unicode.hpp"

#include <algorithm>

namespace {

// Keep useful prefixes and complete UTF-8 code points on small tiles.
std::string compactTileText(const std::string& text, int width, int fontPx) {
  double used = 0;
  size_t end = 0;
  while (end < text.size()) {
    size_t next = utf8Next(text, end);
    unsigned char c = static_cast<unsigned char>(text[end]);
    double advance = fontPx * (c >= 128 ? 1.0 : c == 'i' || c == 'l' || c == '.' ? 0.3 : 0.58);
    if (used + advance > width - 1) break;
    used += advance;
    end = next;
  }
  return text.substr(0, end);
}

std::string tileLabel(const TreeSnapshot& tree, const Tile& tile) {
  if (tile.mergeCount > 0)
    return std::string(zh::remainingPrefix()) + formatCount(tile.mergeCount) + zh::remainingSuffix();
  if (tile.flags & kFree) return zh::freeSpace();
  if (tile.index == kNone || tile.index >= tree.nodes.size()) return {};
  if (tile.flags & kPending) return tree.complete ? std::string(zh::uncounted()) : std::string(zh::scanningRegion());
  return std::string(nodeName(tree, tile.index));
}

}  // namespace

void App::State::renderTreemap(const Input& input, Frame& frame, std::vector<HotRect>& next) {
  State& s = *this;
  const ViewMetrics v(input);
  const int mapX = v.mapX, mapY = v.mapY, mapW = v.mapW, mapH = v.mapH;
  const int gap = v.gap, font = v.font, titlePx = v.titlePx;
  const bool hasTree = s.shown && !s.shown->nodes.empty();
  fillRect(frame, mapX, mapY, mapW, mapH, categoryColor(CatDir));
  if (hasTree) {
    // The view root has its own header, independently of child containers.
    const int rootTitleH = std::min(titlePx, mapH);
    const uint32_t rootBase = categoryColor(CatDir);
    fillRect(frame, mapX, mapY, mapW, rootTitleH, shadeColor(rootBase, 12));
    fillRect(frame, mapX, mapY + rootTitleH - 1, mapW, 1, shadeColor(rootBase, -30));
    const uint64_t rootSize = s.view < s.visible.size() ? s.visible[s.view] : s.shown->nodes[s.view].size;
    std::string rootLabel = std::string(nodeName(*s.shown, s.view)) + "  " + formatSize(rootSize);
    drawText(frame, mapX + 4, mapY, mapW - 8, rootTitleH - 1, rootLabel, 0x1b2631, std::max(9, font - 2), false);
    addHot(next, mapX, mapY, mapW, rootTitleH, HotKind::Tile, s.view);
    RectF area{static_cast<double>(mapX), static_cast<double>(mapY + rootTitleH),
               static_cast<double>(mapW), static_cast<double>(std::max(0, mapH - rootTitleH))};
    const int labelFont = std::max(9, scaled(input.dpi, 10)), compactFont = std::max(8, scaled(input.dpi, 8));
    const double minW = compactFont * 2 + 4, minH = compactFont + 4;
    bool layoutChanged = false;
    const auto& laid = s.layoutCache.get(s.shown, s.visible, s.visibleRevision, s.view, area,
                                         titlePx, minW, minH, s.expanded, s.options, layoutChanged);
    if (layoutChanged || s.snapLayout)
      s.motion.setTarget(laid, input.timeSec, s.view, area, s.snapLayout);
    s.snapLayout = false;
    s.tiles = s.motion.sample(input.timeSec);
    s.frameAnim = s.motion.running(input.timeSec);
    if (s.shown->changeGen != s.seenChange) {
      s.seenChange = s.shown->changeGen;
      for (uint32_t id : s.shown->changed) s.changes[id] = input.timeSec;
    }
    for (auto it = s.changes.begin(); it != s.changes.end();) {
      if (input.timeSec - it->second >= 0.4) it = s.changes.erase(it);
      else ++it;
    }
    s.frameAnim = s.frameAnim || !s.changes.empty();
#if defined(__APPLE__)
    // macOS 显示链路直接使用 CPU 像素，批量填充可避免 OpenCL 的整帧上传与读回。
    // 导航动画期间可能存在重叠图块，需逐块绘制，保留填充与边框的原有顺序。
    const bool layered = std::any_of(s.tiles.begin(), s.tiles.end(), [](const Tile& tile) { return tile.exiting; });
    const bool prefilled = !layered && mapW >= 64 && mapH >= 64 && s.tiles.size() >= 24;
    if (prefilled)
      fillTilePixels(frame, s.tiles, mapX, mapY, mapW, mapH, 1, s.options.fileGroups);
#else
    // 重叠的导航图层必须逐块绘制填充和边框，不能先统一填充再绘制所有边框。
    const bool layered = std::any_of(s.tiles.begin(), s.tiles.end(), [](const Tile& tile) { return tile.exiting; });
    const bool prefilled = !layered && openclFillTiles(frame, s.tiles, mapX, mapY, mapW, mapH, 1, s.options.fileGroups);
#endif
    int hoverTile = -1;
    for (int i = 0; i < static_cast<int>(s.tiles.size()); ++i) {
      const Tile& tile = s.tiles[static_cast<size_t>(i)];
      int tx = iFloor(tile.outer.x), ty = iFloor(tile.outer.y);
      int tw = iFloor(tile.outer.x + tile.outer.w) - tx, th = iFloor(tile.outer.y + tile.outer.h) - ty;
      if (tile.hidden || tw <= 0 || th <= 0) continue;
      if (!tile.exiting && input.mouseX >= tx && input.mouseY >= ty && input.mouseX < tx + tw && input.mouseY < ty + th) hoverTile = i;
      uint32_t base = categoryColor(tile.flags & kDir ? CatDir : s.options.fileGroups ? tile.category : CatOther);
      if (tile.flags & kFree) base = categoryColor(CatFree);
      if (tile.flags & kPending) base = 0xf5cba7;
      const uint32_t titleColor = shadeColor(base, 12), dividerColor = shadeColor(base, -30);
      if (!prefilled) {
        if (th < 8) fillRect(frame, tx, ty, tw, th, base);
        else fillGradient(frame, tx, ty, tw, th, shadeColor(base, 5), shadeColor(base, -5));
      }
      strokeRect(frame, tx, ty, tw, th, shadeColor(base, -30), 1);
      uint32_t labelColor = mixColor(base, 0x1b2631, static_cast<int>(tile.labelOpacity * 255), 255);
      std::string name = tileLabel(*s.shown, tile), sizeText = formatSize(tile.size);
      bool labelVisible = tile.labelOpacity > 0.05;
      if (tile.exiting && labelVisible) {
        // Native text is composited later; suppress labels covered by incoming tiles.
        double cx = tile.outer.x + tile.outer.w * 0.5;
        double cy = tile.container ? tile.title.y + tile.title.h * 0.5 : tile.outer.y + tile.outer.h * 0.5;
        for (size_t j = static_cast<size_t>(i) + 1; j < s.tiles.size(); ++j) {
          const RectF& cover = s.tiles[j].outer;
          if (cx >= cover.x && cy >= cover.y && cx < cover.x + cover.w && cy < cover.y + cover.h) {
            labelVisible = false;
            break;
          }
        }
      }
      const bool compact = tw < labelFont * 10 || th < labelFont * 4;
      const int tileFont = compact ? compactFont : labelFont;
      if (labelVisible && tile.title.h >= compactFont + 1) {
        int hy = iFloor(tile.title.y), hh = iFloor(tile.title.y + tile.title.h) - hy;
        int textH = hh - 1;
        if (!prefilled) {
          fillRect(frame, tx, hy, tw, hh, titleColor);
          fillRect(frame, tx, hy + hh - 1, tw, 1, dividerColor);
        }
        int headerFont = tw < labelFont * 12 ? compactFont : labelFont;
        if (textH >= headerFont && tw >= headerFont * 2 + 4) {
          std::string header = name;
          if (tw >= headerFont * 14) header += "  " + sizeText;
          if (tw < headerFont * 10) header = compactTileText(header, tw - 4, headerFont);
          drawText(frame, tx + 2, hy, tw - 4, textH, header, labelColor, headerFont, false);
        }
      } else if (labelVisible && !tile.container && tw >= tileFont * 2 + 2 && th >= tileFont + 2) {
        std::string shortName = compact ? compactTileText(name, tw - 4, tileFont) : name;
#if defined(__APPLE__)
        const int line = (tileFont * 14 + 9) / 10;
#else
        const int line = tileFont + 1;
#endif
        if (th >= line * 2 + 2 && tw >= tileFont * 4) {
          std::string compactSize = sizeText;
          if (compact) compactSize.erase(std::remove(compactSize.begin(), compactSize.end(), ' '), compactSize.end());
          int y0 = ty + (th - line * 2) / 2;
          drawText(frame, tx + 2, y0, tw - 4, line, shortName, labelColor, tileFont, true);
          drawText(frame, tx + 2, y0 + line, tw - 4, line, compactSize, labelColor, tileFont, true);
        } else drawText(frame, tx + 2, ty + 1, tw - 4, th - 2, shortName, labelColor, tileFont, true);
      }
      if (!tile.exiting) addHot(next, tx, ty, tw, th, HotKind::Tile, tile.mergeCount ? kNone : tile.index);
    }
    // Apply highlights to finished pixels equally for CPU and GPU fills.
    auto brighten = [&](RectF rect, int amount) {
      int x0 = std::max(mapX, iFloor(rect.x)), y0 = std::max(mapY, iFloor(rect.y));
      int x1 = std::min(mapX + mapW, iFloor(rect.x + rect.w)), y1 = std::min(mapY + mapH, iFloor(rect.y + rect.h));
      for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) {
          auto& pixel = frame.bgra[static_cast<size_t>(yy) * frame.width + xx];
          pixel = packRgb(shadeColor(pixel, amount));
        }
      }
    };
    std::vector<int> brightness(s.tiles.size(), 0);
    for (const auto& change : s.changes) {
      uint32_t id = change.first;
      int match = -1;
      while (id != kNone && id < s.shown->nodes.size()) {
        for (size_t i = 0; i < s.tiles.size(); ++i) {
          if (!s.tiles[i].hidden && !s.tiles[i].exiting && s.tiles[i].index == id) { match = static_cast<int>(i); break; }
        }
        if (match >= 0 || id == s.view) break;
        for (size_t i = 0; i < s.tiles.size(); ++i) {
          if (!s.tiles[i].hidden && !s.tiles[i].exiting && s.tiles[i].mergeCount &&
              s.tiles[i].parent == s.shown->nodes[id].parent) { match = static_cast<int>(i); break; }
        }
        if (match >= 0) break;
        id = s.shown->nodes[id].parent;
      }
      double age = input.timeSec - change.second;
      int amount = static_cast<int>(age < 0.12 ? 26 - 18 * age / 0.12 : 8 * (0.4 - age) / 0.28);
      if (match >= 0) brightness[match] = std::max(brightness[match], amount);
      else if (id == s.view) brighten({static_cast<double>(mapX), static_cast<double>(mapY),
                                      static_cast<double>(mapW), static_cast<double>(rootTitleH)}, amount);
    }
    uint64_t hovered = hoverTile < 0 ? static_cast<uint64_t>(kNone) : s.tiles[hoverTile].index != kNone
        ? s.tiles[hoverTile].index : (1ull << 63) | s.tiles[hoverTile].parent;
    if (s.hovered != hovered) { s.hovered = hovered; s.hoverSince = input.timeSec; }
    if (hoverTile >= 0) {
      double age = input.timeSec - s.hoverSince;
      brightness[hoverTile] = std::max(brightness[hoverTile], static_cast<int>(age < 0.12 ? 44 - 18 * age / 0.12 : 26));
      s.frameAnim = s.frameAnim || age < 0.12;
    }
    for (size_t i = 0; i < s.tiles.size(); ++i) {
      if (!s.tiles[i].hidden && !s.tiles[i].exiting && s.tiles[i].index == s.selected) brightness[i] = std::max(brightness[i], 10);
      if (!s.tiles[i].hidden && brightness[i] > 0) brighten(s.tiles[i].outer, brightness[i]);
    }
    if (s.tiles.empty()) {
      std::string message = s.filter.includeGlobs.empty() && s.filter.excludeGlobs.empty() && s.filter.minSize == 0
          ? zh::emptyFolder() : zh::noMatch();
      drawText(frame, mapX, mapY + mapH / 2 - font, mapW, font * 2, message, 0x34495e, font, true);
    }
  } else {
    s.tiles.clear();
    s.motion.setTarget({}, input.timeSec);
    s.frameAnim = s.motion.running(input.timeSec);
    std::string message = s.shown && !s.shown->error.empty() ? s.shown->error : zh::emptyHint();
    fillRect(frame, mapX, mapY, mapW, mapH, 0xf0f0f0);
    if (s.shown && !s.shown->error.empty())
      drawText(frame, mapX + gap, mapY + mapH / 2 - font, mapW - gap * 2, font * 3, message, 0x34495e, font, true);
  }
}
