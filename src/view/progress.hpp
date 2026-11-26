#pragma once

#include "model/tree.hpp"

#include <algorithm>

struct ScanProgress {
  bool determinate = false;
  double fraction = 0;
};

inline ScanProgress scanProgress(const TreeSnapshot* tree) {
  ScanProgress progress;
  if (!tree) return progress;
  if (tree->volumeScan && tree->capacity > tree->freeBytes) {
    const uint64_t totalUsed = tree->capacity - tree->freeBytes;
    const uint64_t remaining = tree->pending < tree->nodes.size() ? tree->nodes[tree->pending].size : 0;
    const uint64_t counted = totalUsed > remaining ? totalUsed - remaining : 0;
    progress.fraction = std::min(1.0, static_cast<double>(counted) / static_cast<double>(totalUsed));
    progress.determinate = true;
  }
  if (tree->complete && !tree->aborted) progress = {true, 1};
  return progress;
}
