#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <unordered_set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

char lowerAscii(char c) {
  if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
  return c;
}

const std::unordered_set<std::string>& extSet(Category cat) {
  static const std::unordered_set<std::string> image = {
      "jpg", "jpeg", "png", "gif", "bmp", "webp", "tif", "tiff", "heic", "ico", "psd", "svg"};
  static const std::unordered_set<std::string> video = {
      "mp4", "mkv", "avi", "mov", "wmv", "webm", "flv", "m4v", "mpg", "mpeg", "ts"};
  static const std::unordered_set<std::string> audio = {
      "mp3", "wav", "flac", "aac", "m4a", "ogg", "wma", "aiff"};
  static const std::unordered_set<std::string> archive = {
      "zip", "rar", "7z", "tar", "gz", "bz2", "xz", "iso", "cab", "tgz"};
  static const std::unordered_set<std::string> doc = {
      "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "txt", "rtf", "md", "csv"};
  static const std::unordered_set<std::string> code = {
      "c", "h", "cpp", "hpp", "cc", "hh", "rs", "py", "js", "ts", "tsx", "jsx", "java",
      "go", "php", "rb", "cs", "swift", "kt", "json", "xml", "yml", "yaml", "html", "css"};
  static const std::unordered_set<std::string> exe = {
      "exe", "dll", "msi", "sys", "so", "dylib", "bin", "app", "apk"};
  switch (cat) {
    case CatImage: return image;
    case CatVideo: return video;
    case CatAudio: return audio;
    case CatArchive: return archive;
    case CatDoc: return doc;
    case CatCode: return code;
    case CatExe: return exe;
    default: break;
  }
  static const std::unordered_set<std::string> empty;
  return empty;
}

}  // namespace

std::string formatSize(uint64_t bytes) {
  static const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
  double value = static_cast<double>(bytes);
  int unit = 0;
  while (value >= 1024.0 && unit < 5) {
    value /= 1024.0;
    ++unit;
  }
  std::ostringstream os;
  os.setf(std::ios::fixed);
  if (unit == 0) {
    os << bytes;
  } else if (value >= 100.0) {
    os.precision(0);
    os << value;
  } else if (value >= 10.0) {
    os.precision(1);
    os << value;
  } else {
    os.precision(2);
    os << value;
  }
  os << ' ' << units[unit];
  return os.str();
}

std::string formatCount(uint64_t n) {
  std::string digits = std::to_string(n);
  std::string out;
  int group = 0;
  for (int i = static_cast<int>(digits.size()) - 1; i >= 0; --i) {
    if (group == 3) {
      out.push_back(',');
      group = 0;
    }
    out.push_back(digits[static_cast<size_t>(i)]);
    ++group;
  }
  std::reverse(out.begin(), out.end());
  return out;
}

bool parseSize(const std::string& text, uint64_t& out) {
  std::string s;
  s.reserve(text.size());
  for (char c : text) {
    if (c != ' ' && c != '\t') s.push_back(c);
  }
  if (s.empty()) {
    out = 0;
    return true;
  }
  size_t i = 0;
  if (s[0] == '+') ++i;
  if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) return false;
  double value = 0;
  while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
    value = value * 10.0 + (s[i] - '0');
    ++i;
  }
  if (i < s.size() && s[i] == '.') {
    ++i;
    double place = 0.1;
    if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
      value += (s[i] - '0') * place;
      place *= 0.1;
      ++i;
    }
  }
  uint64_t mul = 1;
  if (i < s.size()) {
    char u = lowerAscii(s[i]);
    if (u == 'k') mul = 1024ull;
    else if (u == 'm') mul = 1024ull * 1024ull;
    else if (u == 'g') mul = 1024ull * 1024ull * 1024ull;
    else if (u == 't') mul = 1024ull * 1024ull * 1024ull * 1024ull;
    else if (u == 'b') mul = 1;
    else return false;
    ++i;
    if (i < s.size() && lowerAscii(s[i]) == 'b') ++i;
  }
  if (i != s.size()) return false;
  if (value < 0) return false;
  out = static_cast<uint64_t>(value * static_cast<double>(mul) + 0.5);
  return true;
}

std::string formatPercent(uint64_t part, uint64_t whole) {
  if (whole == 0 || part == 0) return "0%";
  double p = static_cast<double>(part) * 100.0 / static_cast<double>(whole);
  std::ostringstream os;
  if (p < 0.1) return "<0.1%";
  os.setf(std::ios::fixed);
  if (p >= 10.0) {
    os.precision(0);
  } else {
    os.precision(1);
  }
  os << p << '%';
  return os.str();
}

std::vector<std::string> splitList(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : text) {
    if (c == ';' || c == '|' || c == ',') {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
    } else if (c != ' ' && c != '\t') {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

bool globMatch(std::string_view text, std::string_view pattern) {
  size_t t = 0;
  size_t p = 0;
  size_t star = static_cast<size_t>(-1);
  size_t match = 0;
  while (t < text.size()) {
    if (p < pattern.size() && (lowerAscii(pattern[p]) == lowerAscii(text[t]) || pattern[p] == '?')) {
      ++t;
      ++p;
    } else if (p < pattern.size() && pattern[p] == '*') {
      star = p++;
      match = t;
    } else if (star != static_cast<size_t>(-1)) {
      p = star + 1;
      t = ++match;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*') ++p;
  return p == pattern.size();
}

bool passNameFilter(std::string_view name, const Filter& filter) {
  if (!filter.includeGlobs.empty()) {
    bool any = false;
    for (const auto& g : filter.includeGlobs) {
      if (globMatch(name, g)) {
        any = true;
        break;
      }
    }
    if (!any) return false;
  }
  for (const auto& g : filter.excludeGlobs) {
    if (globMatch(name, g)) return false;
  }
  return true;
}

uint8_t categoryFromName(std::string_view name) {
  auto dot = name.rfind('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 1 >= name.size()) return CatOther;
  if (name.size() - dot > 8) return CatOther;
  std::string ext;
  ext.reserve(name.size() - dot - 1);
  for (size_t i = dot + 1; i < name.size(); ++i) ext.push_back(lowerAscii(name[i]));
  if (extSet(CatImage).count(ext)) return CatImage;
  if (extSet(CatVideo).count(ext)) return CatVideo;
  if (extSet(CatAudio).count(ext)) return CatAudio;
  if (extSet(CatArchive).count(ext)) return CatArchive;
  if (extSet(CatDoc).count(ext)) return CatDoc;
  if (extSet(CatCode).count(ext)) return CatCode;
  if (extSet(CatExe).count(ext)) return CatExe;
  return CatOther;
}

uint32_t categoryColor(uint8_t category) {
  switch (category) {
    case CatDir: return 0xc7a580;
    case CatImage: return 0x7dcea0;
    case CatVideo: return 0xc39bd3;
    case CatAudio: return 0xf5cba7;
    case CatArchive: return 0xf7dc6f;
    case CatDoc: return 0x85c1e9;
    case CatCode: return 0x76d7c4;
    case CatExe: return 0xf1948a;
    case CatFree: return 0xe5e8eb;
    default: return 0x91b3cf;
  }
}

uint32_t shadeColor(uint32_t rgb, int delta) {
  auto ch = [delta](int c) {
    int v = c + delta;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return v;
  };
  int r = ch((rgb >> 16) & 255);
  int g = ch((rgb >> 8) & 255);
  int b = ch(rgb & 255);
  return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

uint32_t mixColor(uint32_t a, uint32_t b, int tNum, int tDen) {
  if (tDen <= 0) return a;
  auto ch = [&](int shift) {
    int ca = (a >> shift) & 255;
    int cb = (b >> shift) & 255;
    return ca + (cb - ca) * tNum / tDen;
  };
  return (static_cast<uint32_t>(ch(16)) << 16) | (static_cast<uint32_t>(ch(8)) << 8) |
         static_cast<uint32_t>(ch(0));
}

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

std::vector<uint64_t> computeVisible(const TreeSnapshot& tree, const Filter& filter) {
  const size_t n = tree.nodes.size();
  std::vector<uint64_t> vis(n, 0);
  for (size_t i = n; i-- > 0;) {
    const Node& node = tree.nodes[i];
    if (node.flags & kFree) {
      bool plain = filter.includeGlobs.empty() && filter.excludeGlobs.empty() && filter.minSize == 0;
      vis[i] = plain ? node.size : 0;
      continue;
    }
    if (node.flags & kPending) {
      bool plain = filter.includeGlobs.empty() && filter.excludeGlobs.empty() && filter.minSize == 0;
      vis[i] = (plain && node.size > 0) ? node.size : 0;
      continue;
    }
    if (node.flags & kDir) {
      bool excluded = false;
      if (i != 0) {
        for (const auto& g : filter.excludeGlobs) {
          if (globMatch(nodeName(tree, static_cast<uint32_t>(i)), g)) {
            excluded = true;
            break;
          }
        }
      }
      if (excluded) {
        vis[i] = 0;
        continue;
      }
      uint64_t sum = 0;
      for (uint32_t c = node.firstChild; c != kNone && c < n; c = tree.nodes[c].nextSibling) {
        sum += vis[c];
      }
      vis[i] = sum;
    } else {
      if (node.size < filter.minSize) continue;
      if (!passNameFilter(nodeName(tree, static_cast<uint32_t>(i)), filter)) continue;
      vis[i] = node.size;
    }
  }
  return vis;
}

size_t utf8Next(const std::string& s, size_t i) {
  if (i >= s.size()) return s.size();
  unsigned char c = static_cast<unsigned char>(s[i]);
  size_t step = 1;
  if ((c & 0x80u) == 0) step = 1;
  else if ((c & 0xe0u) == 0xc0u) step = 2;
  else if ((c & 0xf0u) == 0xe0u) step = 3;
  else if ((c & 0xf8u) == 0xf0u) step = 4;
  i += step;
  if (i > s.size()) return s.size();
  return i;
}

size_t utf8Prev(const std::string& s, size_t i) {
  if (i == 0) return 0;
  --i;
  while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xc0u) == 0x80u) --i;
  return i;
}

void Frame::resize(int w, int h, uint32_t clear) {
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (w > 8192) w = 8192;
  if (h > 8192) h = 8192;
  width = w;
  height = h;
  bgra.assign(static_cast<size_t>(w) * static_cast<size_t>(h), packRgb(clear));
  texts.clear();
}

#ifdef _WIN32
std::wstring utf8ToWide(const std::string& text) {
  if (text.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], n);
  return out;
}

std::string wideToUtf8(const std::wstring& text) {
  if (text.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr,
                              nullptr);
  if (n <= 0) return {};
  std::string out(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], n, nullptr, nullptr);
  return out;
}
#endif
