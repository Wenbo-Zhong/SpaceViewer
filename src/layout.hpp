#pragma once

#include "types.hpp"

#include <vector>

std::vector<Tile> layoutTreemap(const TreeSnapshot& tree, const std::vector<uint64_t>& visible, uint32_t root,
                                const RectF& area, double titlePx, double minW = 56, double minH = 36,
                                const std::vector<uint32_t>& expanded = {});

// One active branch: ancestors remain open, unrelated branches return to overview.
std::vector<uint32_t> expansionPath(const TreeSnapshot& tree, uint32_t root, uint32_t selected);
