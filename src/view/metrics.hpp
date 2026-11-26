#pragma once

#include "app/input.hpp"
#include "view/tile.hpp"

int scaled(float dpi, int value);
int iFloor(double value);

struct ViewMetrics {
  int width, height, gap, row1, row2, statusH, pathRow, top, progressH;
  int font, titlePx, controlFont;
  int mapX, mapY, mapW, mapH;
  explicit ViewMetrics(const Input& input);
};

RectF directoryContentArea(const Input& input);
