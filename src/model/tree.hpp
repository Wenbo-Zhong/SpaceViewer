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

struct Node {
#if defined(__APPLE__)
  // macOS scans can contain millions of entries. Keep the per-entry record
  // compact because both the working tree and its published snapshot coexist.
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

struct TreeSnapshot {
  std::vector<Node> nodes;
  std::string nameBlob;
  std::string rootPath;
  std::string error;
  std::string tip;
  char sep = '/';
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
  uint64_t changeGen = 0;
  std::vector<uint32_t> changed;
};

struct VolumeEntry {
  std::string path;
  std::string label;
  uint64_t total = 0;
  uint64_t free = 0;
};

std::string_view nodeName(const TreeSnapshot& tree, uint32_t index);
bool nodeHasMultipleChildren(const TreeSnapshot& tree, uint32_t index);
uint32_t addNode(TreeSnapshot& tree, uint32_t parent, const std::string& name, uint8_t flags,
                 uint8_t category, uint64_t size);
void accountBytes(TreeSnapshot& tree, uint32_t dir, uint64_t bytes);
std::string nodePath(const TreeSnapshot& tree, uint32_t index);
