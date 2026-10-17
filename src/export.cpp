#include "export.hpp"

#include "text.hpp"
#include "util.hpp"

#include <cstdio>
#include <vector>

namespace {

void csvEscape(std::string& out, const std::string& value) {
  bool quote = false;
  for (char c : value) {
    if (c == '"' || c == ',' || c == '\n' || c == '\r') quote = true;
  }
  if (!quote) {
    out += value;
    return;
  }
  out.push_back('"');
  for (char c : value) {
    if (c == '"') out.push_back('"');
    out.push_back(c);
  }
  out.push_back('"');
}

FILE* openOut(const std::string& path) {
#if defined(_WIN32)
  std::wstring wide = utf8ToWide(path);
  return _wfopen(wide.c_str(), L"wb");
#else
  return std::fopen(path.c_str(), "wb");
#endif
}

}  // namespace

bool writeCsv(const TreeSnapshot& tree, const Filter& filter, uint32_t root, const std::string& path,
              std::string& error) {
  if (tree.nodes.empty() || root >= tree.nodes.size()) {
    error = zh::exportFail();
    return false;
  }
  FILE* file = openOut(path);
  if (!file) {
    error = zh::exportFail();
    return false;
  }
  const unsigned char bom[] = {0xef, 0xbb, 0xbf};
  std::fwrite(bom, 1, 3, file);
  std::string header;
  csvEscape(header, zh::colPath());
  header.push_back(',');
  csvEscape(header, zh::colName());
  header.push_back(',');
  csvEscape(header, zh::colType());
  header.push_back(',');
  csvEscape(header, zh::colBytes());
  header.push_back(',');
  csvEscape(header, zh::colSize());
  header.push_back(',');
  csvEscape(header, zh::colPercent());
  header += "\r\n";
  std::fwrite(header.data(), 1, header.size(), file);

  std::vector<uint64_t> visible = computeVisible(tree, filter);
  uint64_t whole = root < visible.size() ? visible[root] : 0;
  std::vector<uint32_t> stack;
  stack.push_back(root);
  std::string line;
  line.reserve(256);
  while (!stack.empty()) {
    uint32_t id = stack.back();
    stack.pop_back();
    if (id >= tree.nodes.size()) continue;
    if (id != root && (id >= visible.size() || visible[id] == 0)) continue;
    const Node& node = tree.nodes[id];
    if (node.flags & kDir) {
      std::vector<uint32_t> children;
      for (uint32_t child = node.firstChild; child != kNone && child < tree.nodes.size();
           child = tree.nodes[child].nextSibling) {
        children.push_back(child);
      }
      for (size_t i = children.size(); i-- > 0;) stack.push_back(children[i]);
    }
    std::string type;
    if (node.flags & kFree) type = zh::freeSpace();
    else if (node.flags & kPending) type = zh::uncounted();
    else if (node.flags & kDir) type = zh::dirType();
    else type = zh::fileType();
    uint64_t size = id < visible.size() && !(node.flags & (kFree | kPending)) ? visible[id] : node.size;
    if ((node.flags & (kFree | kPending)) && (id >= visible.size() || visible[id] == 0) && id != root) continue;
    line.clear();
    csvEscape(line, nodePath(tree, id));
    line.push_back(',');
    csvEscape(line, std::string(nodeName(tree, id)));
    line.push_back(',');
    csvEscape(line, type);
    line.push_back(',');
    line += std::to_string(size);
    line.push_back(',');
    csvEscape(line, formatSize(size));
    line.push_back(',');
    csvEscape(line, formatPercent(size, whole));
    line += "\r\n";
    std::fwrite(line.data(), 1, line.size(), file);
  }
  std::fclose(file);
  return true;
}
