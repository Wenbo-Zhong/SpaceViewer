#include "model/tree.hpp"

std::string_view nodeName(const TreeSnapshot& tree, uint32_t index) {
  if (index >= tree.nodes.size()) return {};
  const Node& n = tree.nodes[index];
  if (n.nameOff + n.nameLen > tree.nameBlob.size()) return {};
  return std::string_view(tree.nameBlob.data() + n.nameOff, n.nameLen);
}

bool nodeHasMultipleChildren(const TreeSnapshot& tree, uint32_t index) {
  if (index >= tree.nodes.size()) return false;
#if defined(__APPLE__)
  uint32_t first = tree.nodes[index].firstChild;
  return first != kNone && first < tree.nodes.size() && tree.nodes[first].nextSibling != kNone;
#else
  return tree.nodes[index].childCount > 1;
#endif
}

uint32_t addNode(TreeSnapshot& tree, uint32_t parent, const std::string& name, uint8_t flags,
                 uint8_t category, uint64_t size) {
  Node node;
  node.nameOff = static_cast<uint32_t>(tree.nameBlob.size());
  node.nameLen = static_cast<decltype(node.nameLen)>(name.size());
  tree.nameBlob.append(name);
  node.parent = parent;
  node.flags = flags;
  node.category = category;
  node.size = size;
  uint32_t id = static_cast<uint32_t>(tree.nodes.size());
  tree.nodes.push_back(node);
  if (parent != kNone && parent < id) {
    Node& p = tree.nodes[parent];
    if (p.firstChild == kNone) p.firstChild = id;
    else tree.nodes[p.lastChild].nextSibling = id;
    p.lastChild = id;
#if !defined(__APPLE__)
    p.childCount++;
#endif
  }
  return id;
}

void accountBytes(TreeSnapshot& tree, uint32_t dir, uint64_t bytes) {
  if (bytes == 0 || tree.nodes.empty()) return;
  uint32_t i = dir;
  while (i != kNone && i < tree.nodes.size()) {
    tree.nodes[i].size += bytes;
    i = tree.nodes[i].parent;
  }
  if (tree.pending != kNone && tree.pending < tree.nodes.size()) {
    uint64_t have = tree.nodes[tree.pending].size;
    uint64_t dec = bytes < have ? bytes : have;
    tree.nodes[tree.pending].size = have - dec;
    if (tree.nodes[0].size >= dec) tree.nodes[0].size -= dec;
    else tree.nodes[0].size = 0;
  }
}

std::string nodePath(const TreeSnapshot& tree, uint32_t index) {
  if (tree.nodes.empty() || index >= tree.nodes.size()) return {};
  if (tree.nodes[index].flags & (kFree | kPending)) return {};
  if (index == 0) return tree.rootPath;
  std::vector<std::string_view> parts;
  uint32_t i = index;
  while (i != 0 && i != kNone && i < tree.nodes.size()) {
    parts.push_back(nodeName(tree, i));
    i = tree.nodes[i].parent;
  }
  std::string out = tree.rootPath;
  for (size_t n = parts.size(); n-- > 0;) {
    if (!out.empty() && out.back() != '\\' && out.back() != '/') out.push_back(tree.sep);
    out.append(parts[n].data(), parts[n].size());
  }
  return out;
}
