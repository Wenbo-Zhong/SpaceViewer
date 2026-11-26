#include "scan_support.hpp"

namespace spaceviewer::apple {

bool shouldSkipDirectory(const std::string& scanRoot, const std::string& path) {
  // 启动根目录已通过 firmlink 暴露可写数据，继续遍历 Data 卷会重复统计同一批文件。
  // 仅扫描 / 时跳过该路径，用户直接选择 Data 卷时仍允许扫描。
  return scanRoot == "/" && path == "/System/Volumes/Data";
}

}  // namespace spaceviewer::apple
