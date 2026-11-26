#include "scan/state.hpp"

#include <poll.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>

using spaceviewer::scan::bubble;
using spaceviewer::scan::joinPath;

void Scanner::Impl::watch() {
  int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (fd < 0) return;
  std::unordered_map<int, uint32_t> watchOf;
  auto arm = [&](uint32_t id, const std::string& path) {
    if (watchOf.size() >= 4096) {
      std::lock_guard<std::mutex> lock(treeMu);
      live.watchGap = true;
      return;
    }
    if (stayOnRootDevice) {
      struct stat st;
      if (stat(path.c_str(), &st) != 0 || st.st_dev != rootDevice) return;
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
        live.changed.push_back(removeNode(id));
        continue;
      }
      struct stat st;
      if (lstat(full.c_str(), &st) != 0 || S_ISLNK(st.st_mode)) continue;
      bool isDir = S_ISDIR(st.st_mode);
      if (stayOnRootDevice && st.st_dev != rootDevice) {
        if (isDir) {
          std::lock_guard<std::mutex> lock(treeMu);
          uint32_t id = addNode(live, parent, name, kDir, CatDir, 0);
          live.dirs++;
          live.changed.push_back(id);
        }
        continue;
      }
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
        scanSerial(id, full, false);
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
}
