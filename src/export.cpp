#include "export.hpp"

#include "text.hpp"
#include "util.hpp"

#include <cstdio>
#include <fstream>
#include <filesystem>
#include <iomanip>
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

bool writeSnapshot(const TreeSnapshot& tree, const std::string& path, std::string& error) {
  std::ofstream out(std::filesystem::u8path(path), std::ios::binary);
  if (!out) { error = "Cannot create snapshot"; return false; }
  std::vector<uint32_t> ids;
  std::vector<uint32_t> remap(tree.nodes.size(), kNone);
  if (!tree.nodes.empty()) { ids.push_back(0); remap[0] = 0; }
  for (size_t cursor = 0; cursor < ids.size(); ++cursor) {
    uint32_t child = tree.nodes[ids[cursor]].firstChild;
    for (size_t guard = 0; child != kNone && child < tree.nodes.size() && guard < tree.nodes.size(); ++guard) {
      if (remap[child] == kNone) { remap[child] = static_cast<uint32_t>(ids.size()); ids.push_back(child); }
      child = tree.nodes[child].nextSibling;
    }
  }
  uint32_t pending = tree.pending < remap.size() ? remap[tree.pending] : kNone;
  out << "SpaceViewer-Snapshot-1\n" << std::quoted(tree.rootPath) << "\n"
      << static_cast<int>(tree.sep) << ' ' << tree.capacity << ' ' << tree.freeBytes << ' '
      << tree.files << ' ' << tree.dirs << ' ' << pending << ' ' << ids.size() << "\n";
  for (uint32_t i : ids) {
    const Node& n = tree.nodes[i];
    uint32_t parent = n.parent < remap.size() ? remap[n.parent] : kNone;
    out << parent << ' ' << n.size << ' ' << static_cast<int>(n.flags) << ' '
        << static_cast<int>(n.category) << ' ' << std::quoted(std::string(nodeName(tree, i))) << "\n";
  }
  out.close();
  if (!out) { error = "Cannot finish writing snapshot"; return false; }
  return true;
}

bool readSnapshot(const std::string& path, TreeSnapshot& tree, std::string& error) {
  std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
  in.seekg(0,std::ios::end);
  auto length = in.tellg();
  if (length < 0 || length > 512ll*1024*1024) { error = "Snapshot is unavailable or too large"; return false; }
  in.seekg(0);
  std::string magic;
  std::getline(in, magic);
  if (magic != "SpaceViewer-Snapshot-1") { error = "Not a SpaceViewer snapshot (.svs)"; return false; }
  TreeSnapshot loaded;
  int sep = 0;
  uint64_t count = 0;
  if (!(in >> std::quoted(loaded.rootPath) >> sep >> loaded.capacity >> loaded.freeBytes >>
        loaded.files >> loaded.dirs >> loaded.pending >> count) || count == 0 || count > 5000000 ||
      loaded.rootPath.size() > 32768 || (sep != '/' && sep != 92)) {
    error = "Invalid snapshot header"; return false;
  }
  loaded.sep = static_cast<char>(sep);
  for (uint64_t i = 0; i < count; ++i) {
    uint32_t parent; uint64_t size; int flags, category; std::string name;
    if (!(in >> parent >> size >> flags >> category >> std::quoted(name)) ||
        (i == 0 ? parent != kNone : parent >= i || !(loaded.nodes[parent].flags & kDir)) ||
        flags < 0 || flags > 7 || category < CatOther || category > CatFree || name.size() > 32768) {
      error = "Invalid snapshot node"; return false;
    }
    addNode(loaded, parent, name, static_cast<uint8_t>(flags), static_cast<uint8_t>(category), size);
  }
  if (loaded.pending != kNone && loaded.pending >= loaded.nodes.size()) {
    error = "Invalid pending-space node"; return false;
  }
  loaded.complete = true;
  tree = std::move(loaded);
  return true;
}
