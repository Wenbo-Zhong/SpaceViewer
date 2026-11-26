#include "platform/posix/volume.hpp"

#include <cstdlib>
#include <sys/stat.h>
#include <sys/statvfs.h>

namespace spaceviewer::posix {

bool isVolumeRoot(const std::string& path) {
  if (path == "/") return true;
  struct stat self;
  struct stat parent;
  if (stat(path.c_str(), &self) != 0) return false;
  if (stat((path + "/..").c_str(), &parent) != 0) return false;
  return self.st_dev != parent.st_dev;
}

bool volumeCapacity(const std::string& path, uint64_t& total, uint64_t& available) {
  struct statvfs vfs;
  if (statvfs(path.c_str(), &vfs) != 0) return false;
  total = static_cast<uint64_t>(vfs.f_blocks) * static_cast<uint64_t>(vfs.f_frsize);
  available = static_cast<uint64_t>(vfs.f_bavail) * static_cast<uint64_t>(vfs.f_frsize);
  return true;
}

void addVolume(std::vector<VolumeEntry>& out, const std::string& path, const std::string& label) {
  for (const VolumeEntry& existing : out) {
    if (existing.path == path) return;
  }
  VolumeEntry entry;
  entry.path = path;
  entry.label = label.empty() ? path : label;
  volumeCapacity(path, entry.total, entry.free);
  out.push_back(std::move(entry));
}

std::string normalizePath(const std::string& input) {
  if (input.empty()) return {};
  char buffer[4096];
  if (realpath(input.c_str(), buffer) != nullptr) return buffer;
  std::string path = input;
  while (path.size() > 1 && path.back() == '/') path.pop_back();
  return path;
}

}  // namespace spaceviewer::posix
