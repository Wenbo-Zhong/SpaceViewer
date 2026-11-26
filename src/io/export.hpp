#pragma once

#include "model/filter.hpp"

#include <string>

// 导出指定根节点下的过滤结果，使用原始可见大小，不受图块可读性裁减影响。
bool writeCsv(const TreeSnapshot& tree, const Filter& filter, uint32_t root, const std::string& path,
              std::string& error);

// 快照保存完整可达树，不应用 UI 过滤；读取成功后才替换 tree，失败时保留调用者原数据。
bool writeSnapshot(const TreeSnapshot& tree, const std::string& path, std::string& error);
bool readSnapshot(const std::string& path, TreeSnapshot& tree, std::string& error);
