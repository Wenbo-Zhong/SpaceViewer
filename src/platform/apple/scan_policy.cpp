#include "scan_support.hpp"

namespace spaceviewer::apple {

bool shouldSkipDirectory(const std::string& scanRoot, const std::string& path) {
  // Writable locations are exposed through firmlinks from the startup root.
  // Traversing Data as well would count those files a second time.
  return scanRoot == "/" && path == "/System/Volumes/Data";
}

}  // namespace spaceviewer::apple
