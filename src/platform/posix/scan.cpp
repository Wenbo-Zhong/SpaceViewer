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
  // 扫描 / 时限制在根设备内；挂载点保留为目录，但不递归或统计其他设备上的文件。
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
    // 权限失败同时计入总错误和权限错误，继续扫描其他目录，交由 UI 在结束后提示授权。
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
    // 枚举子项使用 lstat，跳过符号链接本身及其目标，避免循环遍历和重复统计。
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
      // 使用逻辑文件长度，与 Windows 枚举保持一致，不以 st_blocks 估算实际分配空间。
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
