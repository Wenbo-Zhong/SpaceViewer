#include "app/state.hpp"
#include "i18n/text.hpp"
#include "view/layout.hpp"
#include "view/metrics.hpp"

#include <algorithm>
#include <cmath>

void App::State::clickTile(const HotRect& hot, const Input& input, const Host& host,
                           double previousClick, int previousX, int previousY, uint32_t previousSelected) {
  State& s = *this;
  if (hot.index == kNone || !s.shown || hot.index >= s.shown->nodes.size()) {
    s.setNote(zh::mergedNote(), input.timeSec);
    s.lastZoom = false;
    return;
  }
  const Node& node = s.shown->nodes[hot.index];
  s.selected = hot.index;
  bool zoom = (node.flags & kDir) && !(node.flags & (kFree | kPending));
  if (zoom) {
    const uint32_t target = hot.index;
    const std::vector<uint32_t> path = expansionPath(*s.shown, s.view, target);
    const std::vector<uint64_t> layoutVisible =
        visibleForLayout(*s.shown, s.visible, s.options.showUnknown);
    const int compactFont = std::max(8, scaled(input.dpi, 8));
    const double minW = compactFont * 2 + 4, minH = compactFont + 4;
    const double titlePx = std::max(12.0, static_cast<double>(scaled(input.dpi, 14)));
    auto canExpand = [&](uint32_t view, const std::vector<uint32_t>& expanded) {
      RectF area = directoryContentArea(input);
      if (s.options.showFree && view == 0 && s.shown->freeBytes) {
        const double used = layoutVisible.empty() ? 0 : static_cast<double>(layoutVisible[0]);
        const double freeWidth = area.w *
            (static_cast<double>(s.shown->freeBytes) / (used + s.shown->freeBytes));
        if (freeWidth >= minW) area.w -= freeWidth;
      }
      const std::vector<Tile> candidate = layoutTreemap(
          *s.shown, layoutVisible, view, area, titlePx, minW, minH, expanded, s.options.detail);
      return std::any_of(candidate.begin(), candidate.end(), [&](const Tile& tile) {
        return tile.index == target && tile.container && !tile.hidden;
      });
    };
    // Expand in place when possible; otherwise enter the nearest ancestor.
    if (target == s.view || canExpand(s.view, path)) {
      s.expanded = path;
      s.snapLayout = true;
    } else if (!path.empty()) {
      const uint32_t nextView = path.front();
      std::vector<uint32_t> nextExpanded = expansionPath(*s.shown, nextView, target);
      const bool targetFits = nextView == target || canExpand(nextView, nextExpanded);
      s.navigate(nextView);
      // Keep the route open even when the target itself cannot fit.
      if (!targetFits && !nextExpanded.empty()) nextExpanded.pop_back();
      s.expanded = std::move(nextExpanded);
      s.snapLayout = false;
    }
    s.lastZoom = true;
  } else {
    s.lastZoom = false;
    bool again = previousSelected == hot.index && (input.timeSec - previousClick) < 0.35 &&
                 std::abs(input.mouseX - previousX) < 6 && std::abs(input.mouseY - previousY) < 6;
    if (again && host.openPath && !(node.flags & (kFree | kPending))) {
      std::string path = s.pathOf(hot.index);
      if (!path.empty()) host.openPath(path);
    }
  }
}
