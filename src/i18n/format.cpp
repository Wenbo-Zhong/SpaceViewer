#include "i18n/format.hpp"
#include "i18n/unicode.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

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
  if (unit == 0) os << bytes;
  else {
    os.precision(value >= 100.0 ? 0 : value >= 10.0 ? 1 : 2);
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
  os.precision(p >= 10.0 ? 0 : 1);
  os << p << '%';
  return os.str();
}
