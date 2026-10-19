#include "scan.hpp"

#include "text.hpp"
#include "util.hpp"
#if defined(__APPLE__)
#include "platform/apple/scan_support.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#if defined(__linux__)
#include <poll.h>
#include <sys/inotify.h>
#include <unordered_map>
#endif
#if defined(__APPLE__)
#include <CoreServices/CoreServices.h>
#endif
#endif

namespace {

constexpr int kBatch = 2000;

void bubble(TreeSnapshot& tree, uint32_t start, int64_t delta);
void unlinkNode(TreeSnapshot& tree, uint32_t id);
void countSubtree(const TreeSnapshot& tree, uint32_t id, uint64_t& files, uint64_t& dirs);

std::string joinPath(const std::string& dir, const std::string& name, char sep) {
  if (dir.empty()) return name;
  if (dir == "/") return "/" + name;
  if (dir.back() == '/' || dir.back() == '\\') return dir + name;
  std::string out = dir;
  out.push_back(sep);
  out += name;
  return out;
}

#if defined(_WIN32)
std::wstring joinWide(const std::wstring& dir, const std::wstring& name) {
  if (dir.empty()) return name;
  if (!dir.empty() && dir.back() == L'\\') return dir + name;
  return dir + L'\\' + name;
}

std::wstring extendedPath(const std::wstring& path) {
  if (path.rfind(L"\\\\?\\", 0) == 0) return path;
  if (path.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + path.substr(2);
  return L"\\\\?\\" + path;
}

bool isVolumeRoot(const std::wstring& path) {
  if (path.size() == 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) return true;
  std::wstring p = path;
  while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
  if (p.rfind(L"\\\\", 0) != 0) return false;
  int slashes = 0;
  for (wchar_t c : p) {
    if (c == L'\\') ++slashes;
  }
  return slashes == 3;
}

std::wstring normalizeWide(const std::wstring& input) {
  if (input.empty()) return {};
  DWORD need = GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
  std::wstring full(need == 0 ? input.size() + 1 : need, L'\0');
  DWORD wrote = GetFullPathNameW(input.c_str(), static_cast<DWORD>(full.size()), &full[0], nullptr);
  if (wrote == 0) full = input;
  else full.resize(wrote);
  for (wchar_t& c : full) {
    if (c == L'/') c = L'\\';
  }
  if (!isVolumeRoot(full)) {
    while (full.size() > 3 && full.back() == L'\\') full.pop_back();
  } else if (full.size() == 2 && full[1] == L':') {
    full.push_back(L'\\');
  }
  return full;
}
#else
bool isVolumeRoot(const std::string& path) {
  if (path == "/") return true;
  struct stat self;
  struct stat parent;
  if (stat(path.c_str(), &self) != 0) return false;
  std::string up = path + "/..";
  if (stat(up.c_str(), &parent) != 0) return false;
  return self.st_dev != parent.st_dev;
}

std::string normalizePosix(const std::string& input) {
  if (input.empty()) return {};
  char buffer[4096];
  if (realpath(input.c_str(), buffer) != nullptr) return buffer;
  std::string path = input;
  while (path.size() > 1 && path.back() == '/') path.pop_back();
  return path;
}
#endif

}  // namespace

struct Scanner::Impl {
  std::mutex treeMu;
  std::mutex pubMu;
  std::mutex publishMu;
  std::mutex wakeMu;
  std::mutex taskMu;
  std::condition_variable taskCv;
  struct ScanTask {
    uint32_t node;
    std::string path;
    bool chargePending;
  };
  std::deque<ScanTask> taskQueue;
  size_t outstandingTasks = 0;
  TreeSnapshot live;
  std::shared_ptr<const TreeSnapshot> published = std::make_shared<TreeSnapshot>();
  std::function<void()> wake;
  std::thread worker;
  std::atomic<bool> cancel{false};
  std::atomic<bool> runningFlag{false};
  std::chrono::steady_clock::time_point started{};
  std::chrono::steady_clock::time_point lastPub{};

  ~Impl() { join(); }

  void join() {
    cancel = true;
    if (worker.joinable()) worker.join();
    cancel = false;
  }

  void fire() {
    std::function<void()> callback;
    {
      std::lock_guard<std::mutex> lock(wakeMu);
      callback = wake;
    }
    if (callback) callback();
  }

  void publish(bool force) {
    std::lock_guard<std::mutex> publishLock(publishMu);
    auto now = std::chrono::steady_clock::now();
    if (!force && lastPub.time_since_epoch().count() != 0 && now - lastPub < std::chrono::milliseconds(250)) {
      return;
    }
    auto snap = std::make_shared<TreeSnapshot>();
    {
      std::lock_guard<std::mutex> lock(treeMu);
      live.elapsed = std::chrono::duration<double>(now - started).count();
      *snap = live;
    }
    {
      std::lock_guard<std::mutex> lock(pubMu);
      published = snap;
    }
    lastPub = now;
    fire();
  }

  template <typename ScanOne>
  void runTaskPool(const ScanTask& root, ScanOne scanOne) {
    {
      std::lock_guard<std::mutex> lock(taskMu);
      taskQueue.clear();
      taskQueue.push_back(root);
      outstandingTasks = 1;
    }
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    const size_t threadCount = std::max<size_t>(2, std::min<size_t>(8, hardwareThreads ? hardwareThreads : 4));
    auto worker = [&]() {
#if defined(_WIN32)
      SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
      for (;;) {
        ScanTask task{};
        {
          std::unique_lock<std::mutex> lock(taskMu);
          taskCv.wait(lock, [&]() { return cancel.load() || !taskQueue.empty() || outstandingTasks == 0; });
          if (cancel.load() || (taskQueue.empty() && outstandingTasks == 0)) return;
          if (taskQueue.empty()) continue;
          task = std::move(taskQueue.front());
          taskQueue.pop_front();
        }
        std::vector<ScanTask> children = scanOne(task);
        {
          std::lock_guard<std::mutex> lock(taskMu);
          if (!cancel.load()) {
            for (ScanTask& child : children) taskQueue.push_back(std::move(child));
            outstandingTasks += children.size();
          }
          if (outstandingTasks > 0) --outstandingTasks;
          if (cancel.load()) {
            taskQueue.clear();
            outstandingTasks = 0;
          }
        }
        taskCv.notify_all();
      }
    };
    std::vector<std::thread> workers;
    workers.reserve(threadCount - 1);
    for (size_t i = 1; i < threadCount; ++i) workers.emplace_back(worker);
    worker();
    for (std::thread& thread : workers) thread.join();
    {
      std::lock_guard<std::mutex> lock(taskMu);
      taskQueue.clear();
      outstandingTasks = 0;
    }
  }

  void scanParallel(uint32_t id, const std::string& path, bool chargePending) {
    runTaskPool(ScanTask{id, path, chargePending}, [this](const ScanTask& task) {
#if defined(_WIN32)
      return scanWindowsOne(task.node, utf8ToWide(task.path), task.chargePending);
#else
      return scanPosixOne(task.node, task.path, task.chargePending);
#endif
    });
  }

  void threadMain() {
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    std::wstring root;
    {
      std::lock_guard<std::mutex> lock(treeMu);
      root = utf8ToWide(live.rootPath);
    }
    DWORD attr = GetFileAttributesW(extendedPath(root).c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
      std::lock_guard<std::mutex> lock(treeMu);
      live.error = zh::cannotOpen();
      live.running = false;
      live.complete = true;
      runningFlag = false;
    }
    if (!runningFlag) {
      publish(true);
      return;
    }
    if (isVolumeRoot(root)) {
      ULARGE_INTEGER avail{};
      ULARGE_INTEGER total{};
      ULARGE_INTEGER freeTotal{};
      if (GetDiskFreeSpaceExW(root.c_str(), &avail, &total, &freeTotal)) {
        std::lock_guard<std::mutex> lock(treeMu);
        uint64_t capacity = total.QuadPart;
        uint64_t freeBytes = avail.QuadPart;
        if (freeBytes > capacity) freeBytes = capacity;
        uint64_t unknown = capacity > freeBytes ? capacity - freeBytes : 0;
        live.capacity = capacity;
        live.freeBytes = freeBytes;
        live.pending = addNode(live, 0, zh::uncounted(), kPending, CatOther, unknown);
        live.nodes[0].size = unknown;
      }
    }
    publish(true);
    scanParallel(0, wideToUtf8(root), true);
#else
    std::string root;
    {
      std::lock_guard<std::mutex> lock(treeMu);
      root = live.rootPath;
    }
    struct stat st;
    if (stat(root.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
      std::lock_guard<std::mutex> lock(treeMu);
      live.error = zh::cannotOpen();
      live.running = false;
      live.complete = true;
      runningFlag = false;
    }
    if (!runningFlag) {
      publish(true);
      return;
    }
    if (isVolumeRoot(root)) {
      struct statvfs vfs;
      if (statvfs(root.c_str(), &vfs) == 0) {
        uint64_t capacity = static_cast<uint64_t>(vfs.f_blocks) * static_cast<uint64_t>(vfs.f_frsize);
        uint64_t freeBlocks = static_cast<uint64_t>(vfs.f_bavail);
        uint64_t freeBytes = freeBlocks * static_cast<uint64_t>(vfs.f_frsize);
#if defined(__APPLE__)
        uint64_t reportedCapacity = 0;
        uint64_t reportedAvailable = 0;
        if (spaceviewer::apple::volumeCapacity(root, reportedCapacity, reportedAvailable)) {
          capacity = reportedCapacity;
          freeBytes = reportedAvailable;
        }
#endif
        std::lock_guard<std::mutex> lock(treeMu);
        if (freeBytes > capacity) freeBytes = capacity;
        uint64_t unknown = capacity > freeBytes ? capacity - freeBytes : 0;
        live.capacity = capacity;
        live.freeBytes = freeBytes;
        live.pending = addNode(live, 0, zh::uncounted(), kPending, CatOther, unknown);
        live.nodes[0].size = unknown;
      }
    }
    publish(true);
    scanParallel(0, root, true);
#endif
    {
      std::lock_guard<std::mutex> lock(treeMu);
      live.running = false;
      live.complete = true;
      live.aborted = cancel.load();
      live.tip.clear();
    }
    runningFlag = false;
    publish(true);
    if (!cancel.load()) watch();
  }

#if defined(_WIN32)
  std::vector<ScanTask> scanWindowsOne(uint32_t id, const std::wstring& path, bool chargePending) {
    if (cancel) return {};
    {
      std::lock_guard<std::mutex> lock(treeMu);
      live.tip = wideToUtf8(path);
    }
    publish(false);
    std::wstring pattern = extendedPath(path);
    if (pattern.empty() || pattern.back() != L'\\') pattern.push_back(L'\\');
    pattern.push_back(L'*');
    WIN32_FIND_DATAW data;
    HANDLE handle = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
                                     FIND_FIRST_EX_LARGE_FETCH);
    if (handle == INVALID_HANDLE_VALUE) {
      DWORD err = GetLastError();
      if (err != ERROR_FILE_NOT_FOUND && err != ERROR_NO_MORE_FILES) {
        std::lock_guard<std::mutex> lock(treeMu);
        live.errors++;
      }
      return {};
    }
    struct Pending {
      std::string name;
      uint64_t size = 0;
      uint8_t category = CatOther;
      bool dir = false;
      std::wstring wpath;
    };
    std::vector<Pending> batch;
    batch.reserve(kBatch);
    struct Later {
      uint32_t node;
      std::wstring path;
    };
    std::vector<Later> later;
    auto flush = [&]() {
      if (batch.empty()) return;
      std::lock_guard<std::mutex> lock(treeMu);
      uint64_t sum = 0;
      for (const Pending& item : batch) {
        if (item.dir) {
          uint32_t child = addNode(live, id, item.name, kDir, CatDir, 0);
          live.dirs++;
          later.push_back(Later{child, item.wpath});
        } else {
          addNode(live, id, item.name, 0, item.category, item.size);
          live.files++;
          sum += item.size;
        }
      }
      if (sum) {
        if (chargePending) accountBytes(live, id, sum);
        else bubble(live, id, static_cast<int64_t>(sum));
      }
      batch.clear();
    };
    do {
      if (cancel) break;
      const wchar_t* name = data.cFileName;
      if (name[0] == L'.' && (name[1] == 0 || (name[1] == L'.' && name[2] == 0))) continue;
      if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
      Pending item;
      item.name = wideToUtf8(name);
      item.dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
      if (item.dir) {
        item.wpath = joinWide(path, name);
      } else {
        item.size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        item.category = categoryFromName(item.name);
      }
      batch.push_back(std::move(item));
      if (static_cast<int>(batch.size()) >= kBatch) {
        flush();
        publish(false);
      }
    } while (FindNextFileW(handle, &data));
    FindClose(handle);
    flush();
    publish(false);
    std::vector<ScanTask> children;
    children.reserve(later.size());
    for (const Later& dir : later) children.push_back(ScanTask{dir.node, wideToUtf8(dir.path), chargePending});
    return children;
  }

  void scanWindows(uint32_t id, const std::wstring& path, bool chargePending) {
    std::vector<ScanTask> stack{ScanTask{id, wideToUtf8(path), chargePending}};
    while (!stack.empty() && !cancel.load()) {
      ScanTask task = std::move(stack.back());
      stack.pop_back();
      std::vector<ScanTask> children = scanWindowsOne(task.node, utf8ToWide(task.path), task.chargePending);
      stack.insert(stack.end(), std::make_move_iterator(children.begin()), std::make_move_iterator(children.end()));
    }
  }
#else
  std::vector<ScanTask> scanPosixOne(uint32_t id, const std::string& path, bool chargePending) {
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
    struct Pending {
      std::string name;
      uint64_t size = 0;
      uint8_t category = CatOther;
      bool dir = false;
      std::string full;
    };
    std::vector<Pending> batch;
    struct Later {
      uint32_t node;
      std::string path;
    };
    std::vector<Later> later;
    auto flush = [&]() {
      if (batch.empty()) return;
      std::lock_guard<std::mutex> lock(treeMu);
      uint64_t sum = 0;
      for (const Pending& item : batch) {
        if (item.dir) {
          uint32_t child = addNode(live, id, item.name, kDir, CatDir, 0);
          live.dirs++;
          later.push_back(Later{child, item.full});
        } else {
          addNode(live, id, item.name, 0, item.category, item.size);
          live.files++;
          sum += item.size;
        }
      }
      if (sum) {
        if (chargePending) accountBytes(live, id, sum);
        else bubble(live, id, static_cast<int64_t>(sum));
      }
      batch.clear();
    };
    while (dirent* entry = readdir(directory)) {
      if (cancel) break;
      const char* name = entry->d_name;
      if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) continue;
      std::string full = joinPath(path, name, '/');
      struct stat st;
      if (lstat(full.c_str(), &st) != 0) {
        const int statError = errno;
        std::lock_guard<std::mutex> lock(treeMu);
        live.errors++;
        if (statError == EACCES || statError == EPERM) live.permissionErrors++;
        continue;
      }
      if (S_ISLNK(st.st_mode)) continue;
      Pending item;
      item.name = name;
      if (S_ISDIR(st.st_mode)) {
        item.dir = true;
        item.full = full;
      } else if (S_ISREG(st.st_mode)) {
        item.size = static_cast<uint64_t>(st.st_size);
        item.category = categoryFromName(item.name);
      } else {
        continue;
      }
      batch.push_back(std::move(item));
      if (static_cast<int>(batch.size()) >= kBatch) {
        flush();
        publish(false);
      }
    }
    closedir(directory);
    flush();
    publish(false);
    std::vector<ScanTask> children;
    children.reserve(later.size());
    for (const Later& dir : later) children.push_back(ScanTask{dir.node, dir.path, chargePending});
    return children;
  }

  void scanPosix(uint32_t id, const std::string& path, bool chargePending) {
    std::vector<ScanTask> stack{ScanTask{id, path, chargePending}};
    while (!stack.empty() && !cancel.load()) {
      ScanTask task = std::move(stack.back());
      stack.pop_back();
      std::vector<ScanTask> children = scanPosixOne(task.node, task.path, task.chargePending);
      stack.insert(stack.end(), std::make_move_iterator(children.begin()), std::make_move_iterator(children.end()));
    }
  }
#endif

  void watch();
#if defined(__APPLE__)
  void reconcileApple(const std::string& root, char** paths, size_t count);
#endif
};

namespace {

void bubble(TreeSnapshot& tree, uint32_t start, int64_t delta) {
  if (delta == 0) return;
  uint32_t i = start;
  for (int guard = 0; i != kNone && i < tree.nodes.size() && guard < 100000; ++guard) {
    Node& node = tree.nodes[i];
    if (delta > 0) node.size += static_cast<uint64_t>(delta);
    else {
      uint64_t sub = static_cast<uint64_t>(-delta);
      node.size = node.size > sub ? node.size - sub : 0;
    }
    i = node.parent;
  }
}

void unlinkNode(TreeSnapshot& tree, uint32_t id) {
  if (id >= tree.nodes.size()) return;
  uint32_t parent = tree.nodes[id].parent;
  if (parent == kNone || parent >= tree.nodes.size()) return;
  uint32_t prev = kNone;
  uint32_t cur = tree.nodes[parent].firstChild;
  while (cur != kNone && cur != id && cur < tree.nodes.size()) {
    prev = cur;
    cur = tree.nodes[cur].nextSibling;
  }
  if (cur != id) return;
  uint32_t next = tree.nodes[id].nextSibling;
  if (prev == kNone) tree.nodes[parent].firstChild = next;
  else tree.nodes[prev].nextSibling = next;
  if (tree.nodes[parent].lastChild == id) tree.nodes[parent].lastChild = prev;
  if (tree.nodes[parent].childCount > 0) tree.nodes[parent].childCount--;
  tree.nodes[id].parent = kNone;
  tree.nodes[id].nextSibling = kNone;
}

void countSubtree(const TreeSnapshot& tree, uint32_t id, uint64_t& files, uint64_t& dirs) {
  if (id >= tree.nodes.size()) return;
  if (tree.nodes[id].flags & kDir) dirs++;
  else files++;
  for (uint32_t c = tree.nodes[id].firstChild; c != kNone && c < tree.nodes.size(); c = tree.nodes[c].nextSibling) {
    countSubtree(tree, c, files, dirs);
  }
}

}  // namespace

void Scanner::Impl::watch() {
#if defined(_WIN32)
  std::wstring root;
  {
    std::lock_guard<std::mutex> lock(treeMu);
    root = utf8ToWide(live.rootPath);
  }
  HANDLE dir = CreateFileW(extendedPath(root).c_str(), FILE_LIST_DIRECTORY,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
  if (dir == INVALID_HANDLE_VALUE) return;
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!event) {
    CloseHandle(dir);
    return;
  }
  std::vector<char> buffer(64 * 1024);

  auto sameName = [](std::string_view utf8, const std::wstring& wide) {
    std::wstring other = utf8ToWide(std::string(utf8));
    return CompareStringOrdinal(other.c_str(), static_cast<int>(other.size()), wide.c_str(),
                                 static_cast<int>(wide.size()), TRUE) == CSTR_EQUAL;
  };
  auto findChild = [&](uint32_t parent, const std::wstring& name) -> uint32_t {
    if (parent >= live.nodes.size()) return kNone;
    for (uint32_t c = live.nodes[parent].firstChild; c != kNone && c < live.nodes.size();
         c = live.nodes[c].nextSibling) {
      if (live.nodes[c].flags & kPending) continue;
      if (sameName(nodeName(live, c), name)) return c;
    }
    return kNone;
  };
  auto findRel = [&](const std::wstring& rel) -> uint32_t {
    uint32_t id = 0;
    size_t i = 0;
    while (i < rel.size()) {
      size_t slash = rel.find(L'\\', i);
      if (slash == std::wstring::npos) slash = rel.size();
      std::wstring part = rel.substr(i, slash - i);
      if (!part.empty()) {
        id = findChild(id, part);
        if (id == kNone) return kNone;
      }
      i = slash + 1;
    }
    return id;
  };
  bool batchChanged = false;
  auto note = [&](uint32_t id) {
    if (id == kNone) return;
    if (!batchChanged) live.changed.clear();
    batchChanged = true;
    live.changed.push_back(id);
  };
  auto refreshFree = [&]() {
    if (!isVolumeRoot(root)) return;
    ULARGE_INTEGER avail{};
    ULARGE_INTEGER total{};
    ULARGE_INTEGER freeTotal{};
    if (!GetDiskFreeSpaceExW(root.c_str(), &avail, &total, &freeTotal)) return;
    live.capacity = total.QuadPart;
    live.freeBytes = avail.QuadPart;
    if (live.freeBytes > live.capacity) live.freeBytes = live.capacity;
    if (live.pending == kNone || live.pending >= live.nodes.size() || live.nodes.empty()) return;
    uint64_t pendingSize = live.nodes[live.pending].size;
    uint64_t counted = live.nodes[0].size > pendingSize ? live.nodes[0].size - pendingSize : 0;
    uint64_t unknown = 0;
    if (live.capacity > live.freeBytes && live.capacity - live.freeBytes > counted) {
      unknown = live.capacity - live.freeBytes - counted;
    }
    live.nodes[live.pending].size = unknown;
    live.nodes[0].size = counted + unknown;
  };

  OVERLAPPED overlapped{};
  overlapped.hEvent = event;
  auto arm = [&]() {
    ResetEvent(event);
    return ReadDirectoryChangesW(dir, buffer.data(), static_cast<DWORD>(buffer.size()), TRUE,
                                 FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                     FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
                                 nullptr, &overlapped, nullptr) != FALSE;
  };
  bool armed = arm();
  while (!cancel.load() && armed) {
    DWORD got = 0;
    for (;;) {
      DWORD wait = WaitForSingleObject(event, 200);
      if (cancel.load()) break;
      if (wait == WAIT_TIMEOUT) continue;
      if (!GetOverlappedResult(dir, &overlapped, &got, FALSE)) {
        if (GetLastError() == ERROR_NOTIFY_ENUM_DIR) {
          std::lock_guard<std::mutex> lock(treeMu);
          live.watchGap = true;
        }
        publish(true);
        got = 0;
      }
      break;
    }
    if (cancel.load()) break;
    if (got == 0) { armed = arm(); continue; }

    struct Ev {
      DWORD action;
      std::wstring rel;
    };
    std::vector<Ev> events;
    auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer.data());
    for (;;) {
      Ev ev;
      ev.action = info->Action;
      ev.rel.assign(info->FileName, info->FileNameLength / sizeof(wchar_t));
      events.push_back(std::move(ev));
      if (info->NextEntryOffset == 0) break;
      info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(reinterpret_cast<char*>(info) + info->NextEntryOffset);
    }

    // Re-arm before inspecting files: writes that finish while this batch is
    // processed must still generate a subsequent size update.
    armed = arm();
    batchChanged = false;
    for (const Ev& ev : events) {
      if (cancel.load()) break;
      std::wstring full = joinWide(root, ev.rel);
      size_t slash = ev.rel.find_last_of(L'\\');
      std::wstring parentRel = slash == std::wstring::npos ? L"" : ev.rel.substr(0, slash);
      std::wstring base = slash == std::wstring::npos ? ev.rel : ev.rel.substr(slash + 1);
      if (ev.action == FILE_ACTION_REMOVED || ev.action == FILE_ACTION_RENAMED_OLD_NAME) {
        std::lock_guard<std::mutex> lock(treeMu);
        uint32_t id = findRel(ev.rel);
        if (id == kNone || id == 0) continue;
        uint64_t files = 0;
        uint64_t dirs = 0;
        countSubtree(live, id, files, dirs);
        uint64_t size = live.nodes[id].size;
        uint32_t parent = live.nodes[id].parent;
        unlinkNode(live, id);
        bubble(live, parent, -static_cast<int64_t>(size));
        if (live.files >= files) live.files -= files;
        else live.files = 0;
        if (live.dirs >= dirs) live.dirs -= dirs;
        else live.dirs = 0;
        note(parent);
        continue;
      }
      DWORD attr = GetFileAttributesW(extendedPath(full).c_str());
      if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
      bool isDir = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
      if (ev.action == FILE_ACTION_MODIFIED) {
        if (isDir) continue;
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(extendedPath(full).c_str(), GetFileExInfoStandard, &data)) continue;
        uint64_t size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        std::lock_guard<std::mutex> lock(treeMu);
        uint32_t id = findRel(ev.rel);
        if (id == kNone || (live.nodes[id].flags & kDir)) continue;
        int64_t delta = static_cast<int64_t>(size) - static_cast<int64_t>(live.nodes[id].size);
        live.nodes[id].size = size;
        bubble(live, live.nodes[id].parent, delta);
        if (delta != 0) note(id);
        continue;
      }
      uint32_t parent = kNone;
      uint32_t existing = kNone;
      {
        std::lock_guard<std::mutex> lock(treeMu);
        parent = parentRel.empty() ? 0 : findRel(parentRel);
        if (parent != kNone) existing = findChild(parent, base);
      }
      if (parent == kNone) continue;
      if (existing != kNone) {
        if (!isDir) {
          WIN32_FILE_ATTRIBUTE_DATA data{};
          if (!GetFileAttributesExW(extendedPath(full).c_str(), GetFileExInfoStandard, &data)) continue;
          uint64_t size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
          std::lock_guard<std::mutex> lock(treeMu);
          if (existing >= live.nodes.size() || (live.nodes[existing].flags & kDir)) continue;
          int64_t delta = static_cast<int64_t>(size) - static_cast<int64_t>(live.nodes[existing].size);
          live.nodes[existing].size = size;
          bubble(live, parent, delta);
          if (delta != 0) note(existing);
        }
        continue;
      }
      if (isDir) {
        uint32_t id = kNone;
        {
          std::lock_guard<std::mutex> lock(treeMu);
          id = addNode(live, parent, wideToUtf8(base), kDir, CatDir, 0);
          live.dirs++;
          note(id);
        }
        scanWindows(id, full, false);
      } else {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(extendedPath(full).c_str(), GetFileExInfoStandard, &data)) continue;
        uint64_t size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        std::string name = wideToUtf8(base);
        std::lock_guard<std::mutex> lock(treeMu);
        uint32_t id = addNode(live, parent, name, 0, categoryFromName(name), size);
        live.files++;
        bubble(live, parent, static_cast<int64_t>(size));
        note(id);
      }
    }
    {
      std::lock_guard<std::mutex> lock(treeMu);
      refreshFree();
      if (batchChanged) live.changeGen++;
    }
    publish(true);
  }
  CancelIo(dir);
  WaitForSingleObject(event, 50);
  CloseHandle(event);
  CloseHandle(dir);
#elif defined(__linux__)
  std::string root;
  {
    std::lock_guard<std::mutex> lock(treeMu);
    root = live.rootPath;
  }
  int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (fd < 0) return;
  std::unordered_map<int, uint32_t> watchOf;
  auto arm = [&](uint32_t id, const std::string& path) {
    if (watchOf.size() >= 4096) {
      std::lock_guard<std::mutex> lock(treeMu);
      live.watchGap = true;
      return;
    }
    int wd = inotify_add_watch(fd, path.c_str(),
                               IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_ONLYDIR);
    if (wd >= 0) watchOf[wd] = id;
  };
  {
    std::lock_guard<std::mutex> lock(treeMu);
    std::vector<uint32_t> stack{0};
    while (!stack.empty() && watchOf.size() < 4096) {
      uint32_t id = stack.back();
      stack.pop_back();
      if (id >= live.nodes.size() || (live.nodes[id].flags & kDir) == 0) continue;
      if (live.nodes[id].flags & kPending) continue;
      std::string path = nodePath(live, id);
      if (!path.empty()) {
        int wd = inotify_add_watch(fd, path.c_str(),
                                   IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_ONLYDIR);
        if (wd >= 0) watchOf[wd] = id;
      }
      for (uint32_t c = live.nodes[id].firstChild; c != kNone && c < live.nodes.size(); c = live.nodes[c].nextSibling) {
        if (live.nodes[c].flags & kDir) stack.push_back(c);
      }
    }
    if (watchOf.size() >= 4096) live.watchGap = true;
  }
  std::vector<char> buffer(64 * 1024);
  while (!cancel.load()) {
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    int pr = poll(&pfd, 1, 200);
    if (cancel.load()) break;
    if (pr <= 0) continue;
    int n = static_cast<int>(read(fd, buffer.data(), buffer.size()));
    if (n <= 0) continue;
    {
      std::lock_guard<std::mutex> lock(treeMu);
      live.changed.clear();
    }
    int offset = 0;
    while (offset < n) {
      auto* ev = reinterpret_cast<inotify_event*>(buffer.data() + offset);
      offset += static_cast<int>(sizeof(inotify_event) + ev->len);
      if (ev->mask & IN_Q_OVERFLOW) {
        std::lock_guard<std::mutex> lock(treeMu);
        live.watchGap = true;
        continue;
      }
      auto found = watchOf.find(ev->wd);
      if (found == watchOf.end() || ev->len == 0) continue;
      std::string name = ev->name;
      if (name == "." || name == "..") continue;
      uint32_t parent = found->second;
      std::string full;
      {
        std::lock_guard<std::mutex> lock(treeMu);
        if (parent >= live.nodes.size()) continue;
        full = nodePath(live, parent);
      }
      full = joinPath(full, name, '/');
      if (ev->mask & (IN_DELETE | IN_MOVED_FROM)) {
        std::lock_guard<std::mutex> lock(treeMu);
        uint32_t id = kNone;
        for (uint32_t c = live.nodes[parent].firstChild; c != kNone && c < live.nodes.size();
             c = live.nodes[c].nextSibling) {
          if (nodeName(live, c) == name) {
            id = c;
            break;
          }
        }
        if (id == kNone) continue;
        uint64_t files = 0;
        uint64_t dirs = 0;
        countSubtree(live, id, files, dirs);
        uint64_t size = live.nodes[id].size;
        unlinkNode(live, id);
        bubble(live, parent, -static_cast<int64_t>(size));
        if (live.files >= files) live.files -= files;
        else live.files = 0;
        if (live.dirs >= dirs) live.dirs -= dirs;
        else live.dirs = 0;
        live.changed.push_back(parent);
        continue;
      }
      struct stat st;
      if (lstat(full.c_str(), &st) != 0 || S_ISLNK(st.st_mode)) continue;
      bool isDir = S_ISDIR(st.st_mode);
      uint32_t existing = kNone;
      {
        std::lock_guard<std::mutex> lock(treeMu);
        if (parent >= live.nodes.size()) continue;
        for (uint32_t c = live.nodes[parent].firstChild; c != kNone && c < live.nodes.size();
             c = live.nodes[c].nextSibling) {
          if (nodeName(live, c) == name) {
            existing = c;
            break;
          }
        }
      }
      if (existing != kNone && !isDir && (ev->mask & (IN_MODIFY | IN_MOVED_TO | IN_CREATE))) {
        std::lock_guard<std::mutex> lock(treeMu);
        if (existing < live.nodes.size() && (live.nodes[existing].flags & kDir) == 0) {
          int64_t delta = static_cast<int64_t>(st.st_size) - static_cast<int64_t>(live.nodes[existing].size);
          live.nodes[existing].size = static_cast<uint64_t>(st.st_size);
          bubble(live, parent, delta);
          live.changed.push_back(existing);
        }
        continue;
      }
      if (existing != kNone) continue;
      if (isDir) {
        uint32_t id = kNone;
        {
          std::lock_guard<std::mutex> lock(treeMu);
          id = addNode(live, parent, name, kDir, CatDir, 0);
          live.dirs++;
          live.changed.push_back(id);
        }
        arm(id, full);
        scanPosix(id, full, false);
      } else if (S_ISREG(st.st_mode)) {
        std::lock_guard<std::mutex> lock(treeMu);
        uint32_t id = addNode(live, parent, name, 0, categoryFromName(name), static_cast<uint64_t>(st.st_size));
        live.files++;
        bubble(live, parent, static_cast<int64_t>(st.st_size));
        live.changed.push_back(id);
      }
    }
    {
      std::lock_guard<std::mutex> lock(treeMu);
      if (!live.changed.empty()) live.changeGen++;
    }
    publish(true);
  }
  close(fd);
#elif defined(__APPLE__)
  std::string root;
  {
    std::lock_guard<std::mutex> lock(treeMu);
    root = live.rootPath;
  }
  struct Ctx {
    Impl* impl;
    std::string root;
  } ctx{this, root};
  CFStringRef cfPath = CFStringCreateWithCString(nullptr, root.c_str(), kCFStringEncodingUTF8);
  CFArrayRef paths = CFArrayCreate(nullptr, reinterpret_cast<const void**>(&cfPath), 1, &kCFTypeArrayCallBacks);
  FSEventStreamContext context{};
  context.info = &ctx;
  FSEventStreamRef stream = FSEventStreamCreate(
      nullptr,
      [](ConstFSEventStreamRef, void* info, size_t num, void* eventPaths, const FSEventStreamEventFlags*,
         const FSEventStreamEventId*) {
        auto* ctx = static_cast<Ctx*>(info);
        auto** paths = static_cast<char**>(eventPaths);
        ctx->impl->reconcileApple(ctx->root, paths, num);
      },
      &context, paths, kFSEventStreamEventIdSinceNow, 0.3,
      kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer);
  CFRelease(paths);
  CFRelease(cfPath);
  if (!stream) return;
  FSEventStreamScheduleWithRunLoop(stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
  FSEventStreamStart(stream);
  while (!cancel.load()) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.2, true);
  FSEventStreamStop(stream);
  FSEventStreamInvalidate(stream);
  FSEventStreamRelease(stream);
#else
  while (!cancel.load()) std::this_thread::sleep_for(std::chrono::milliseconds(200));
#endif
}

#if defined(__APPLE__)
void Scanner::Impl::reconcileApple(const std::string& root, char** paths, size_t count) {
  {
    std::lock_guard<std::mutex> lock(treeMu);
    live.changed.clear();
  }
  for (size_t n = 0; n < count; ++n) {
    std::string full = paths[n];
    while (full.size() > 1 && full.back() == '/') full.pop_back();
    if (full.compare(0, root.size(), root) != 0) continue;
    std::string rel = full.size() > root.size() ? full.substr(root.size()) : std::string();
    while (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());
    if (rel.empty()) continue;
    struct stat st;
    bool gone = lstat(full.c_str(), &st) != 0 || S_ISLNK(st.st_mode);
    std::string parentRel;
    std::string leaf = rel;
    auto slash = rel.find_last_of('/');
    if (slash != std::string::npos) {
      parentRel = rel.substr(0, slash);
      leaf = rel.substr(slash + 1);
    }
    auto locate = [&](const std::string& relative) -> uint32_t {
      uint32_t id = 0;
      size_t i = 0;
      while (i < relative.size()) {
        size_t cut = relative.find('/', i);
        if (cut == std::string::npos) cut = relative.size();
        std::string part = relative.substr(i, cut - i);
        uint32_t child = kNone;
        if (!part.empty() && id < live.nodes.size()) {
          for (uint32_t c = live.nodes[id].firstChild; c != kNone && c < live.nodes.size();
               c = live.nodes[c].nextSibling) {
            if (nodeName(live, c) == part) {
              child = c;
              break;
            }
          }
        }
        if (child == kNone) return kNone;
        id = child;
        i = cut + 1;
      }
      return id;
    };
    if (gone) {
      std::lock_guard<std::mutex> lock(treeMu);
      uint32_t id = locate(rel);
      if (id == kNone || id == 0) continue;
      uint64_t files = 0;
      uint64_t dirs = 0;
      countSubtree(live, id, files, dirs);
      uint32_t up = live.nodes[id].parent;
      uint64_t size = live.nodes[id].size;
      unlinkNode(live, id);
      bubble(live, up, -static_cast<int64_t>(size));
      if (live.files >= files) live.files -= files;
      else live.files = 0;
      if (live.dirs >= dirs) live.dirs -= dirs;
      else live.dirs = 0;
      live.changed.push_back(up);
      continue;
    }
    uint32_t parent = kNone;
    uint32_t existing = kNone;
    {
      std::lock_guard<std::mutex> lock(treeMu);
      parent = parentRel.empty() ? 0 : locate(parentRel);
      if (parent != kNone) {
        for (uint32_t c = live.nodes[parent].firstChild; c != kNone && c < live.nodes.size();
             c = live.nodes[c].nextSibling) {
          if (nodeName(live, c) == leaf) {
            existing = c;
            break;
          }
        }
      }
    }
    if (parent == kNone) continue;
    if (existing != kNone && S_ISREG(st.st_mode)) {
      std::lock_guard<std::mutex> lock(treeMu);
      if (existing < live.nodes.size() && (live.nodes[existing].flags & kDir) == 0) {
        int64_t delta = static_cast<int64_t>(st.st_size) - static_cast<int64_t>(live.nodes[existing].size);
        live.nodes[existing].size = static_cast<uint64_t>(st.st_size);
        bubble(live, parent, delta);
        live.changed.push_back(existing);
      }
      continue;
    }
    if (existing != kNone) continue;
    if (S_ISDIR(st.st_mode)) {
      uint32_t id = kNone;
      {
        std::lock_guard<std::mutex> lock(treeMu);
        id = addNode(live, parent, leaf, kDir, CatDir, 0);
        live.dirs++;
        live.changed.push_back(id);
      }
      scanPosix(id, full, false);
    } else if (S_ISREG(st.st_mode)) {
      std::lock_guard<std::mutex> lock(treeMu);
      uint32_t id = addNode(live, parent, leaf, 0, categoryFromName(leaf), static_cast<uint64_t>(st.st_size));
      live.files++;
      bubble(live, parent, static_cast<int64_t>(st.st_size));
      live.changed.push_back(id);
    }
  }
  {
    std::lock_guard<std::mutex> lock(treeMu);
    if (!live.changed.empty()) live.changeGen++;
  }
  publish(true);
}
#endif

Scanner::Scanner() : impl(new Impl) {}

Scanner::~Scanner() {
  impl->join();
  delete impl;
}

void Scanner::setWake(std::function<void()> wake) {
  std::lock_guard<std::mutex> lock(impl->wakeMu);
  impl->wake = std::move(wake);
}

void Scanner::start(const std::string& utf8Path) {
  impl->join();
#if defined(_WIN32)
  std::wstring full = normalizeWide(utf8ToWide(utf8Path));
  std::string path = wideToUtf8(full);
  char sep = '\\';
#else
  std::string path = normalizePosix(utf8Path);
  char sep = '/';
#endif
  if (path.empty()) return;
  {
    std::lock_guard<std::mutex> lock(impl->treeMu);
    impl->live = TreeSnapshot{};
    impl->live.rootPath = path;
    impl->live.sep = sep;
    impl->live.running = true;
    impl->live.volumeScan = isVolumeRoot(path);
    std::string rootName = path;
    auto slash = rootName.find_last_of("\\/");
    if (slash != std::string::npos && slash + 1 < rootName.size()) rootName = rootName.substr(slash + 1);
    addNode(impl->live, kNone, rootName, kDir, CatDir, 0);
  }
  impl->cancel = false;
  impl->runningFlag = true;
  impl->started = std::chrono::steady_clock::now();
  impl->lastPub = {};
  impl->publish(true);
  impl->worker = std::thread([this]() { impl->threadMain(); });
}

void Scanner::refreshSubtree(uint32_t index) {
  auto current = snapshot();
  if (!current || index >= current->nodes.size() || !(current->nodes[index].flags & kDir)) return;
  if (index == 0) { start(current->rootPath); return; }
  impl->join();
  std::string path = nodePath(*current,index);
  {
    std::lock_guard<std::mutex> lock(impl->treeMu);
    uint64_t files = 0, dirs = 0;
    countSubtree(impl->live,index,files,dirs);
    bubble(impl->live,index,-static_cast<int64_t>(impl->live.nodes[index].size));
    impl->live.files -= std::min(files,impl->live.files);
    impl->live.dirs -= std::min(dirs > 0 ? dirs-1 : 0,impl->live.dirs);
    Node& node = impl->live.nodes[index];
    node.firstChild = node.lastChild = kNone; node.childCount = 0;
    impl->live.running = true; impl->live.complete = false; impl->live.aborted = false;
    impl->live.volumeScan = false;
  }
  impl->cancel = false; impl->runningFlag = true;
  impl->started = std::chrono::steady_clock::now(); impl->lastPub = {};
  impl->publish(true);
  impl->worker = std::thread([this,index,path]() {
#if defined(_WIN32)
    impl->scanParallel(index, path, false);
#else
    impl->scanParallel(index, path, false);
#endif
    {
      std::lock_guard<std::mutex> lock(impl->treeMu);
      impl->live.running = false; impl->live.complete = true; impl->live.aborted = impl->cancel.load();
      impl->live.changed = {index}; ++impl->live.changeGen;
    }
    impl->runningFlag = false; impl->publish(true);
    if (!impl->cancel.load()) impl->watch();
  });
}

void Scanner::loadSnapshot(TreeSnapshot tree) {
  impl->join();
  tree.complete = true;
  tree.running = false;
  tree.changeGen = 0;
  tree.changed.clear();
  impl->runningFlag = false;
  impl->started = std::chrono::steady_clock::now();
  { std::lock_guard<std::mutex> lock(impl->treeMu); impl->live = std::move(tree); }
  impl->publish(true);
}

void Scanner::requestStop() {
  impl->cancel = true;
  impl->taskCv.notify_all();
}

bool Scanner::running() const { return impl->runningFlag.load(); }

bool Scanner::stopRequested() const { return impl->cancel.load() && impl->runningFlag.load(); }

std::shared_ptr<const TreeSnapshot> Scanner::snapshot() const {
  std::lock_guard<std::mutex> lock(impl->pubMu);
  return impl->published;
}
