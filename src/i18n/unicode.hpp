#pragma once

#include <cstdint>
#include <string>

inline constexpr char lowerAscii(char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

inline constexpr bool isChinese(uint32_t cp) {
  return (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) ||
         (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0x20000 && cp <= 0x2FA1F) ||
         (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0xFE30 && cp <= 0xFE4F) ||
         (cp >= 0xFF00 && cp <= 0xFFEF);
}

size_t utf8Next(const std::string& s, size_t i);
size_t utf8Prev(const std::string& s, size_t i);
