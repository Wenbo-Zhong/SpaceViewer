#include "scan/state.hpp"

#include <algorithm>

using spaceviewer::scan::bubble;
using spaceviewer::scan::countSubtree;

Scanner::Impl::~Impl() { join(); }

void Scanner::Impl::join() {
  cancel = true;
  if (worker.joinable()) worker.join();
  cancel = false;
}

void Scanner::Impl::fire() {
  std::function<void()> callback;
  {
    std::lock_guard<std::mutex> lock(wakeMu);
    callback = wake;
  }
  if (callback) callback();
}

void Scanner::Impl::publish(bool force) {
  std::lock_guard<std::mutex> publishLock(publishMu);
  auto now = std::chrono::steady_clock::now();
#if defined(__APPLE__)
  constexpr auto publishInterval = std::chrono::milliseconds(1000);
#else
  constexpr auto publishInterval = std::chrono::milliseconds(250);
#endif
  if (!force && lastPub.time_since_epoch().count() != 0 && now - lastPub < publishInterval) return;
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

void Scanner::Impl::threadMain() {
  lowerThreadPriority();
  std::string root;
  {
    std::lock_guard<std::mutex> lock(treeMu);
    root = live.rootPath;
  }
  if (!initializeRoot(root)) {
    publish(true);
    return;
  }
  publish(true);
  scanParallel(0, root, true);
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
  std::string path = spaceviewer::scan::normalizePath(utf8Path);
  if (path.empty()) return;
  {
    std::lock_guard<std::mutex> lock(impl->treeMu);
    impl->live = TreeSnapshot{};
    impl->live.rootPath = path;
#if defined(_WIN32)
    impl->live.sep = '\\';
#else
    impl->live.sep = '/';
#endif
    impl->live.running = true;
    impl->live.volumeScan = spaceviewer::scan::isVolumeRoot(path);
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
  std::string path = nodePath(*current, index);
  {
    std::lock_guard<std::mutex> lock(impl->treeMu);
    uint64_t files = 0, dirs = 0;
    countSubtree(impl->live, index, files, dirs);
    bubble(impl->live, index, -static_cast<int64_t>(impl->live.nodes[index].size));
    impl->live.files -= std::min(files, impl->live.files);
    impl->live.dirs -= std::min(dirs > 0 ? dirs - 1 : 0, impl->live.dirs);
    Node& node = impl->live.nodes[index];
    node.firstChild = node.lastChild = kNone;
#if !defined(__APPLE__)
    node.childCount = 0;
#endif
    impl->live.running = true;
    impl->live.complete = false;
    impl->live.aborted = false;
    impl->live.volumeScan = false;
  }
  impl->cancel = false;
  impl->runningFlag = true;
  impl->started = std::chrono::steady_clock::now();
  impl->lastPub = {};
  impl->publish(true);
  impl->worker = std::thread([this, index, path]() {
    impl->scanParallel(index, path, false);
    {
      std::lock_guard<std::mutex> lock(impl->treeMu);
      impl->live.running = false;
      impl->live.complete = true;
      impl->live.aborted = impl->cancel.load();
      impl->live.changed = {index};
      ++impl->live.changeGen;
    }
    impl->runningFlag = false;
    impl->publish(true);
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
