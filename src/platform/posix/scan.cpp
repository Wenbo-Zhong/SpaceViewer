#include "scan/state.hpp"
#include "platform/posix/volume.hpp"
#include "i18n/text.hpp"
#if defined(__APPLE__)
#include "platform/apple/scan_support.hpp"
#endif

#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>

namespace spaceviewer::scan {
std::string normalizePath(const std::string& input) { return posix::normalizePath(input); }
bool isVolumeRoot(const std::string& path) { return posix::isVolumeRoot(path); }
}  // namespace spaceviewer::scan

void Scanner::Impl::lowerThreadPriority() {}

bool Scanner::Impl::initializeRoot(const std::string& root) {
  struct stat st;
  if (stat(root.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
    std::lock_guard<std::mutex> lock(treeMu);
    live.error = zh::cannotOpen();
    live.running = false;
    live.complete = true;
    runningFlag = false;
    return false;
  }
#if defined(__linux__)
  stayOnRootDevice = root == "/";
  rootDevice = st.st_dev;
#endif
  if (spaceviewer::posix::isVolumeRoot(root)) {
    uint64_t capacity = 0, freeBytes = 0;
    if (spaceviewer::posix::volumeCapacity(root, capacity, freeBytes)) {
#if defined(__APPLE__)
      uint64_t reportedCapacity = 0;
      uint64_t reportedAvailable = 0;
      if (spaceviewer::apple::volumeCapacity(root, reportedCapacity, reportedAvailable)) {
        capacity = reportedCapacity;
        freeBytes = reportedAvailable;
      }
#endif
      initializeVolume(capacity, freeBytes);
    }
  }
  return true;
}

std::vector<Scanner::Impl::ScanTask> Scanner::Impl::scanOne(const ScanTask& task) {
  const uint32_t id = task.node;
  const std::string& path = task.path;
  if (cancel) return {};
#if defined(__APPLE__)
  if (path == "/System/Volumes/Data") {
    std::string scanRoot;
    {
      std::lock_guard<std::mutex> lock(treeMu);
      scanRoot = live.rootPath;
    }
    if (spaceviewer::apple::shouldSkipDirectory(scanRoot, path)) return {};
  }
#endif
  {
    std::lock_guard<std::mutex> lock(treeMu);
    live.tip = path;
  }
  publish(false);
  DIR* directory = opendir(path.c_str());
  if (!directory) {
    const int openError = errno;
    std::lock_guard<std::mutex> lock(treeMu);
    live.errors++;
    if (openError == EACCES || openError == EPERM) live.permissionErrors++;
    return {};
  }
  std::vector<Entry> batch;
  std::vector<ScanTask> children;
  while (dirent* entry = readdir(directory)) {
    if (cancel) break;
    const char* name = entry->d_name;
    if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) continue;
    std::string full = spaceviewer::scan::joinPath(path, name, '/');
    struct stat st;
    if (lstat(full.c_str(), &st) != 0) {
      const int statError = errno;
      std::lock_guard<std::mutex> lock(treeMu);
      live.errors++;
      if (statError == EACCES || statError == EPERM) live.permissionErrors++;
      continue;
    }
    if (S_ISLNK(st.st_mode)) continue;
    Entry item;
    item.name = name;
    if (S_ISDIR(st.st_mode)) {
      item.dir = true;
      item.path = full;
#if defined(__linux__)
      item.traverse = !stayOnRootDevice || st.st_dev == rootDevice;
#endif
    } else if (S_ISREG(st.st_mode)) {
#if defined(__linux__)
      if (stayOnRootDevice && st.st_dev != rootDevice) continue;
#endif
      item.size = static_cast<uint64_t>(st.st_size);
      item.category = categoryFromName(item.name);
    } else {
      continue;
    }
    batch.push_back(std::move(item));
    if (static_cast<int>(batch.size()) >= spaceviewer::scan::kBatch) {
      flushBatch(id, task.chargePending, batch, children);
      publish(false);
    }
  }
  closedir(directory);
  flushBatch(id, task.chargePending, batch, children);
  publish(false);
  return children;
}
