#pragma once

#include "app/input.hpp"

enum class Focus { None, Path, Inc, Exc, Min };

struct Edit {
  std::string text;
  // UTF-8 字节偏移；移动和删除须通过 utf8Prev/utf8Next 保持码点边界。
  size_t cursor = 0;
  bool selectAll = false;
};

void insertText(Edit& edit, const std::string& text);
void editKey(Edit& edit, const Input& input);
