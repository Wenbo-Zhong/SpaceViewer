#pragma once

#include <cstdint>
#include <string>

namespace spaceviewer::apple {

bool volumeCapacity(const std::string& path, uint64_t& total, uint64_t& available);
bool shouldSkipDirectory(const std::string& scanRoot, const std::string& path);

}  // namespace spaceviewer::apple
