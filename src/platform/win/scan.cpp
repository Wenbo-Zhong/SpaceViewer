#include "scan/state.hpp"
#include "platform/win/encoding.hpp"
#include "platform/win/scan_path.hpp"
#include "i18n/text.hpp"

#include <windows.h>

namespace spaceviewer::win {

std::wstring joinWide(const std::wstring& dir, const std::wstring& name) {
  if (dir.empty()) return name;
  if (dir.back() == L'\\') return dir + name;
  return dir + L'\\' + name;
}

std::wstring extendedPath(const std::wstring& path) {
  // 扫描使用 Win32 长路径前缀；UNC 路径须保留网络共享形式，不能直接拼接普通前缀。
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

}  // namespace spaceviewer::win

namespace spaceviewer::scan {
std::string normalizePath(const std::string& input) { return wideToUtf8(win::normalizeWide(utf8ToWide(input))); }
bool isVolumeRoot(const std::string& path) { return win::isVolumeRoot(utf8ToWide(path)); }
}  // namespace spaceviewer::scan

void Scanner::Impl::lowerThreadPriority() {
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
}

bool Scanner::Impl::initializeRoot(const std::string& utf8Root) {
  std::wstring root = utf8ToWide(utf8Root);
  DWORD attr = GetFileAttributesW(spaceviewer::win::extendedPath(root).c_str());
  if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    std::lock_guard<std::mutex> lock(treeMu);
    live.error = zh::cannotOpen();
    live.running = false;
    live.complete = true;
    runningFlag = false;
    return false;
  }
  if (spaceviewer::win::isVolumeRoot(root)) {
    ULARGE_INTEGER avail{};
    ULARGE_INTEGER total{};
    ULARGE_INTEGER freeTotal{};
    if (GetDiskFreeSpaceExW(root.c_str(), &avail, &total, &freeTotal)) {
      initializeVolume(total.QuadPart, avail.QuadPart);
    }
  }
  return true;
}

std::vector<Scanner::Impl::ScanTask> Scanner::Impl::scanOne(const ScanTask& task) {
  if (cancel) return {};
  const std::wstring path = utf8ToWide(task.path);
  {
    std::lock_guard<std::mutex> lock(treeMu);
    live.tip = wideToUtf8(path);
  }
  publish(false);
  std::wstring pattern = spaceviewer::win::extendedPath(path);
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
  std::vector<Entry> batch;
  batch.reserve(spaceviewer::scan::kBatch);
  std::vector<ScanTask> children;
  do {
    if (cancel) break;
    const wchar_t* name = data.cFileName;
    if (name[0] == L'.' && (name[1] == 0 || (name[1] == L'.' && name[2] == 0))) continue;
    // 跳过重解析点，包括目录联接和符号链接，避免循环及同一目标的重复计数。
    if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
    Entry item;
    item.name = wideToUtf8(name);
    item.dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (item.dir) {
      item.path = wideToUtf8(spaceviewer::win::joinWide(path, name));
    } else {
      item.size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
      item.category = categoryFromName(item.name);
    }
    batch.push_back(std::move(item));
    if (static_cast<int>(batch.size()) >= spaceviewer::scan::kBatch) {
      flushBatch(task.node, task.chargePending, batch, children);
      publish(false);
    }
  } while (FindNextFileW(handle, &data));
  FindClose(handle);
  flushBatch(task.node, task.chargePending, batch, children);
  publish(false);
  return children;
}
