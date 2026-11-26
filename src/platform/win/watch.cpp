#include "scan/state.hpp"
#include "platform/win/encoding.hpp"
#include "platform/win/scan_path.hpp"

#include <windows.h>

using spaceviewer::scan::bubble;
using spaceviewer::win::extendedPath;
using spaceviewer::win::isVolumeRoot;
using spaceviewer::win::joinWide;

void Scanner::Impl::watch() {
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

  // 按 Windows 的大小写不敏感规则匹配现有节点，避免仅名称大小写变化产生重复节点。
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
    // 先复制通知及 UTF-16 名称，重新挂起监听后 buffer 可能被系统写入下一批事件。
    auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer.data());
    for (;;) {
      Ev ev;
      ev.action = info->Action;
      ev.rel.assign(info->FileName, info->FileNameLength / sizeof(wchar_t));
      events.push_back(std::move(ev));
      if (info->NextEntryOffset == 0) break;
      info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(reinterpret_cast<char*>(info) + info->NextEntryOffset);
    }

    // 检查磁盘前立即挂起下一次监听，让后续写入仍能产生大小变更通知。
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
        note(removeNode(id));
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
        scanSerial(id, wideToUtf8(full), false);
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
}
