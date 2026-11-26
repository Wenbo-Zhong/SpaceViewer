#pragma once

#include "model/tree.hpp"

enum class HotKind {
  Open, Drives, Back, Forward, Up, Rescan, Stop, Export,
  EditPath, EditInc, EditExc, EditMin, DriveItem,
  Tile, Home, FocusRescan, LessDetail, MoreDetail, FileGroups,
  ShowFree, ShowUnknown, Configure, CloseView
};

struct HotRect {
  int x = 0, y = 0, w = 0, h = 0;
  HotKind kind = HotKind::Tile;
  uint32_t index = kNone;
  int driveIndex = -1;
};

struct MenuRequest {
  int x = 0;
  int y = 0;
  bool canOpen = false;
  bool canReveal = false;
  bool canCopy = false;
  bool canZoom = false;
};

enum class MenuCommand { None = 0, Open = 1, Reveal = 2, CopyPath = 3, Zoom = 4 };

inline bool inside(const HotRect& r, int x, int y) {
  return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

inline int hitHot(const std::vector<HotRect>& hots, int x, int y) {
  for (int i = static_cast<int>(hots.size()) - 1; i >= 0; --i) {
    if (inside(hots[static_cast<size_t>(i)], x, y)) return i;
  }
  return -1;
}

inline void addHot(std::vector<HotRect>& hots, int x, int y, int w, int h, HotKind kind,
                   uint32_t index = kNone, int drive = -1) {
  hots.push_back({x, y, w, h, kind, index, drive});
}
