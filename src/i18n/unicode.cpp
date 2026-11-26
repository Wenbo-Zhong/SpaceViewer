#include "i18n/unicode.hpp"

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
