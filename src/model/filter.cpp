#include "model/filter.hpp"
#include "i18n/unicode.hpp"

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
