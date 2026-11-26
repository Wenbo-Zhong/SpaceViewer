#pragma once

#include "view/layout.hpp"
#include "view/options.hpp"

#include <memory>

// Snapshots are immutable; visibleRevision changes whenever filtering is recomputed.
class TreemapLayoutCache {
 public:
  const std::vector<Tile>& get(const std::shared_ptr<const TreeSnapshot>& tree,
                              const std::vector<uint64_t>& visible, uint64_t visibleRevision,
                              uint32_t root, RectF area, double titlePx, double minW, double minH,
                              const std::vector<uint32_t>& expanded, const ViewOptions& options,
                              bool& changed);

 private:
  std::shared_ptr<const TreeSnapshot> tree_;
  uint64_t visibleRevision_ = 0;
  bool showUnknown_ = true;
  std::vector<uint64_t> adjustedVisible_;
  bool valid_ = false;
  uint32_t root_ = kNone;
  RectF area_;
  double titlePx_ = 0, minW_ = 0, minH_ = 0;
  bool showFree_ = false;
  int detail_ = 0;
  std::vector<uint32_t> expanded_;
  std::vector<Tile> tiles_;
};
