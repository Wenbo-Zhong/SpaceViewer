#pragma once

#include "model/tree.hpp"

namespace spaceviewer::posix {

bool isVolumeRoot(const std::string& path);
bool volumeCapacity(const std::string& path, uint64_t& total, uint64_t& available);
void addVolume(std::vector<VolumeEntry>& out, const std::string& path, const std::string& label);
std::string normalizePath(const std::string& input);

}  // namespace spaceviewer::posix
