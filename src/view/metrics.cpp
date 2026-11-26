#include "view/metrics.hpp"

#include <algorithm>
#include <cmath>

int scaled(float dpi, int value) {
  return static_cast<int>(std::lround(static_cast<double>(value) * static_cast<double>(dpi) / 96.0));
}

int iFloor(double value) { return static_cast<int>(std::floor(value)); }

ViewMetrics::ViewMetrics(const Input& input)
    : width(std::max(1, input.windowW)), height(std::max(1, input.windowH)),
      gap(scaled(input.controlDpi(), 8)), row1(scaled(input.controlDpi(), 64)),
      row2(scaled(input.controlDpi(), 30)), statusH(scaled(input.dpi, 26)),
      pathRow(scaled(input.controlDpi(), 32) + gap),
      top(gap + row1 + gap + pathRow + row2 + gap),
      progressH(scaled(input.controlDpi(), 4)), font(std::max(11, scaled(input.dpi, 13))),
      titlePx(std::max(12, scaled(input.dpi, 14))),
      controlFont(std::max(11, scaled(input.controlDpi(), 13))),
      mapX(gap), mapY(top + progressH), mapW(std::max(1, width - gap * 2)),
      mapH(std::max(1, height - statusH - mapY - gap)) {}

RectF directoryContentArea(const Input& input) {
  const ViewMetrics v(input);
  const int mapH = std::max(0, v.height - v.statusH - v.mapY - v.gap);
  const int title = std::min(v.titlePx, mapH);
  return {static_cast<double>(v.mapX), static_cast<double>(v.mapY + title),
          static_cast<double>(v.mapW), static_cast<double>(std::max(0, mapH - title))};
}
