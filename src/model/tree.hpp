#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "model/category.hpp"

constexpr uint32_t kNone = 0xffffffffu;

enum NodeFlags : uint8_t {
  kDir = 1,
  kFree = 2,
  kPending = 4
};

// 树通过数组下标和兄弟链关联节点；同一棵树内节点追加后编号保持稳定。
struct Node {
#if defined(__APPLE__)
  // macOS 扫描可能包含数百万条目，工作树与发布快照同时存在，需保持单节点紧凑。
  // 调整字段时同时检查下面的 32 字节断言，避免放大整棵树的内存占用。
  uint64_t size = 0;
  uint32_t nameOff = 0;
  uint32_t parent = kNone;
  uint32_t firstChild = kNone;
  uint32_t lastChild = kNone;
  uint32_t nextSibling = kNone;
  uint16_t nameLen = 0;
  uint8_t flags = 0;
  uint8_t category = CatOther;
#else
  uint32_t nameOff = 0;
  uint32_t nameLen = 0;
  uint32_t parent = kNone;
  uint32_t firstChild = kNone;
  uint32_t lastChild = kNone;
  uint32_t nextSibling = kNone;
  uint32_t childCount = 0;
  uint64_t size = 0;
  uint8_t flags = 0;
  uint8_t category = CatOther;
#endif
};
#if defined(__APPLE__)
static_assert(sizeof(Node) == 32, "macOS scan nodes should stay compact");
#endif

// 扫描器内部维护可变工作树，发布给 UI 后通过 shared_ptr<const TreeSnapshot> 只读访问。
// 名称集中存储在 nameBlob 中，Node 仅保存 UTF-8 字节的偏移和长度。
struct TreeSnapshot {
  std::vector<Node> nodes;
  std::string nameBlob;
  std::string rootPath;
  std::string error;
  std::string tip;
  char sep = '/';
  // 卷扫描中的“尚未统计”虚拟节点，表示已用空间中尚未归属到文件的部分。
  uint32_t pending = kNone;
  uint64_t capacity = 0;
  uint64_t freeBytes = 0;
  uint64_t files = 0;
  uint64_t dirs = 0;
  uint64_t errors = 0;
  uint64_t permissionErrors = 0;
  double elapsed = 0;
  bool running = false;
  bool complete = false;
  bool aborted = false;
  bool volumeScan = false;
  bool watchGap = false;
  // 监听批次产生变化时递增；changed 用于短暂高亮，不替代完整的树数据。
  uint64_t changeGen = 0;
  std::vector<uint32_t> changed;
};

struct VolumeEntry {
  std::string path;
  std::string label;
  uint64_t total = 0;
  uint64_t free = 0;
};

// 返回借用 nameBlob 的视图，使用期间须保留快照，且不能修改其名称缓冲区。
std::string_view nodeName(const TreeSnapshot& tree, uint32_t index);
bool nodeHasMultipleChildren(const TreeSnapshot& tree, uint32_t index);
uint32_t addNode(TreeSnapshot& tree, uint32_t parent, const std::string& name, uint8_t flags,
                 uint8_t category, uint64_t size);
void accountBytes(TreeSnapshot& tree, uint32_t dir, uint64_t bytes);
std::string nodePath(const TreeSnapshot& tree, uint32_t index);
