#include "scan/state.hpp"
#include "i18n/text.hpp"

#include <algorithm>

namespace spaceviewer::scan {

std::string joinPath(const std::string& dir, const std::string& name, char sep) {
  if (dir.empty()) return name;
  if (dir == "/") return "/" + name;
  if (dir.back() == '/' || dir.back() == '\\') return dir + name;
  std::string out = dir;
  out.push_back(sep);
  out += name;
  return out;
}

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
  // 删除仅修改父子和兄弟链接，保留数组位置；压缩数组会使 UI 的节点编号失效。
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
#if !defined(__APPLE__)
  if (tree.nodes[parent].childCount > 0) tree.nodes[parent].childCount--;
#endif
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

}  // namespace spaceviewer::scan

void Scanner::Impl::flushBatch(uint32_t id, bool chargePending, std::vector<Entry>& batch,
                              std::vector<ScanTask>& children) {
  if (batch.empty()) return;
  // 文件元数据已在锁外读取，此处一次提交一批节点并汇总大小，减少逐文件争锁。
  std::lock_guard<std::mutex> lock(treeMu);
  uint64_t sum = 0;
  for (const Entry& item : batch) {
    if (item.dir) {
      uint32_t child = addNode(live, id, item.name, kDir, CatDir, 0);
      live.dirs++;
      if (item.traverse) children.push_back(ScanTask{child, item.path, chargePending});
    } else {
      addNode(live, id, item.name, 0, item.category, item.size);
      live.files++;
      sum += item.size;
    }
  }
  if (sum) {
    if (chargePending) accountBytes(live, id, sum);
    else spaceviewer::scan::bubble(live, id, static_cast<int64_t>(sum));
  }
  batch.clear();
}

// 调用者须持有 treeMu；删除整条可达子树后返回父节点，供监听变更高亮使用。
uint32_t Scanner::Impl::removeNode(uint32_t id) {
  uint64_t files = 0, dirs = 0;
  spaceviewer::scan::countSubtree(live, id, files, dirs);
  uint64_t size = live.nodes[id].size;
  uint32_t parent = live.nodes[id].parent;
  spaceviewer::scan::unlinkNode(live, id);
  spaceviewer::scan::bubble(live, parent, -static_cast<int64_t>(size));
  live.files -= std::min(files, live.files);
  live.dirs -= std::min(dirs, live.dirs);
  return parent;
}

void Scanner::Impl::initializeVolume(uint64_t capacity, uint64_t available) {
  std::lock_guard<std::mutex> lock(treeMu);
  uint64_t freeBytes = std::min(available, capacity);
  // 先把已用空间归入未知节点，初次扫描发现文件后由 accountBytes 逐步转出。
  uint64_t unknown = capacity - freeBytes;
  live.capacity = capacity;
  live.freeBytes = freeBytes;
  live.pending = addNode(live, 0, zh::uncounted(), kPending, CatOther, unknown);
  live.nodes[0].size = unknown;
}
