#pragma once

#include "model/tree.hpp"

struct RectF {
  double x = 0, y = 0, w = 0, h = 0;
};

struct Tile {
  uint32_t index = kNone;
  uint32_t parent = kNone;
  uint64_t size = 0;
  uint32_t mergeCount = 0;
  uint8_t flags = 0;
  uint8_t category = CatOther;
  RectF outer;
  RectF title;
  double labelOpacity = 1;
  bool exiting = false;
  bool container = false;
  bool hidden = false;
};
