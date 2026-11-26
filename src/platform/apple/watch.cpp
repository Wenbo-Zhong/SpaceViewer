#include "scan/state.hpp"

#include <CoreServices/CoreServices.h>
#include <sys/stat.h>

using spaceviewer::scan::bubble;

void Scanner::Impl::watch() {
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
  // FSEvents 回调在扫描协调线程的 RunLoop 上处理，系统资源退出监听后统一释放。
  FSEventStreamScheduleWithRunLoop(stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
  FSEventStreamStart(stream);
  while (!cancel.load()) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.2, true);
  FSEventStreamStop(stream);
  FSEventStreamInvalidate(stream);
  FSEventStreamRelease(stream);
}

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
    // 通知只提供变更路径，按当前文件状态校准树；路径已删除或变成符号链接时移除旧节点。
    bool gone = lstat(full.c_str(), &st) != 0 || S_ISLNK(st.st_mode);
    std::string parentRel;
    std::string leaf = rel;
    auto slash = rel.find_last_of('/');
    if (slash != std::string::npos) {
      parentRel = rel.substr(0, slash);
      leaf = rel.substr(slash + 1);
    }
    // 查找直接读取工作树，以下所有调用都须位于 treeMu 的保护范围内。
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
      live.changed.push_back(removeNode(id));
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
      // 新目录的磁盘读取在树锁外进行，补扫不再扣减初次扫描的未知空间。
      scanSerial(id, full, false);
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
