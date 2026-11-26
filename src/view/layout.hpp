#pragma once

#include "view/tile.hpp"

#include <vector>

std::vector<Tile> layoutTreemap(const TreeSnapshot& tree, const std::vector<uint64_t>& visible, uint32_t root,
                                const RectF& area, double titlePx, double minW = 56, double minH = 36,
                                const std::vector<uint32_t>& expanded = {}, int detail = 2);

// 手动展开只保留一条活动分支：祖先保持展开，其他分支恢复为总览状态。
std::vector<uint32_t> expansionPath(const TreeSnapshot& tree, uint32_t root, uint32_t selected);

std::vector<uint64_t> visibleForLayout(const TreeSnapshot& tree, const std::vector<uint64_t>& visible,
                                      bool showUnknown);
