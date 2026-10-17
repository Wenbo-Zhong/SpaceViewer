#pragma once

#include "types.hpp"

#include <vector>

std::vector<Tile> layoutTreemap(const TreeSnapshot& tree, const std::vector<uint64_t>& visible, uint32_t root,
                                const RectF& area, double titlePx, double minW = 56, double minH = 36);
