#include "view/layout_cache.hpp"

const std::vector<Tile>& TreemapLayoutCache::get(
    const std::shared_ptr<const TreeSnapshot>& tree, const std::vector<uint64_t>& visible,
    uint64_t visibleRevision, uint32_t root, RectF area, double titlePx, double minW, double minH,
    const std::vector<uint32_t>& expanded, const ViewOptions& options, bool& changed) {
  const bool sizesChanged = tree_ != tree || visibleRevision_ != visibleRevision ||
                            showUnknown_ != options.showUnknown;
  changed = sizesChanged || !valid_ || root_ != root || area_.x != area.x || area_.y != area.y ||
            area_.w != area.w || area_.h != area.h || titlePx_ != titlePx || minW_ != minW ||
            minH_ != minH || expanded_ != expanded || detail_ != options.detail ||
            showFree_ != options.showFree;
  if (!changed) return tiles_;
  if (sizesChanged) {
    tree_ = tree;  // Retain identity even if the scanner replaces its snapshot.
    visibleRevision_ = visibleRevision;
    showUnknown_ = options.showUnknown;
    adjustedVisible_ = tree && !showUnknown_ ? visibleForLayout(*tree, visible, false)
                                            : std::vector<uint64_t>{};
  }
  root_ = root; area_ = area;
  titlePx_ = titlePx; minW_ = minW; minH_ = minH;
  expanded_ = expanded; detail_ = options.detail; showFree_ = options.showFree;
  valid_ = true;
  tiles_.clear();
  if (!tree) return tiles_;
  const auto& sizes = showUnknown_ ? visible : adjustedVisible_;
  RectF content = area;
  double freeWidth = 0;
  if (showFree_ && root == 0 && tree->freeBytes) {
    double used = sizes.empty() ? 0 : static_cast<double>(sizes[0]);
    freeWidth = area.w * (static_cast<double>(tree->freeBytes) / (used + tree->freeBytes));
    if (freeWidth < minW) freeWidth = 0;
    content.w -= freeWidth;
  }
  tiles_ = layoutTreemap(*tree, sizes, root, content, titlePx, minW, minH, expanded, detail_);
  if (freeWidth >= minW) {
    Tile free;
    free.flags = kFree; free.category = CatFree; free.size = tree->freeBytes;
    free.outer = {area.x + content.w, area.y, freeWidth, area.h};
    tiles_.push_back(free);
  }
  return tiles_;
}
