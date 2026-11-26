#pragma once

#include "app/input.hpp"

enum class Focus { None, Path, Inc, Exc, Min };

struct Edit {
  std::string text;
  size_t cursor = 0;
  bool selectAll = false;
};

void insertText(Edit& edit, const std::string& text);
void editKey(Edit& edit, const Input& input);
