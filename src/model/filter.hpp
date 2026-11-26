#pragma once

#include "model/tree.hpp"

struct Filter {
  std::vector<std::string> includeGlobs;
  std::vector<std::string> excludeGlobs;
  uint64_t minSize = 0;
};

std::vector<std::string> splitList(const std::string& text);
bool globMatch(std::string_view text, std::string_view pattern);
bool passNameFilter(std::string_view name, const Filter& filter);
std::vector<uint64_t> computeVisible(const TreeSnapshot& tree, const Filter& filter);
