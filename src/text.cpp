#include "text.hpp"
#include "locales_generated.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace zh {
namespace {

std::string normalizeLocale(std::string value) {
  const size_t suffix = value.find_first_of(".@");
  if (suffix != std::string::npos) value.resize(suffix);
  std::replace(value.begin(), value.end(), '_', '-');
  for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  const size_t firstDash = value.find('-');
  std::string language = value.substr(0, firstDash);
  if (language == "zh") {
    bool traditional = value.find("hant") != std::string::npos || value.find("-tw") != std::string::npos ||
                       value.find("-hk") != std::string::npos || value.find("-mo") != std::string::npos;
    return traditional ? "zh-Hant" : "zh-Hans";
  }
  size_t part = firstDash == std::string::npos ? value.size() : firstDash + 1;
  while (part < value.size()) {
    size_t end = value.find('-', part);
    if (end == std::string::npos) end = value.size();
    if (end - part == 2) {
      for (size_t i = part; i < end; ++i) value[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[i])));
    } else if (end - part == 4) {
      value[part] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[part])));
    }
    part = end + 1;
  }
  return value;
}

std::string detectLocale() {
#if defined(_WIN32)
  wchar_t wide[LOCALE_NAME_MAX_LENGTH] = {};
  LCID uiLocale = MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT);
  if (LCIDToLocaleName(uiLocale, wide, LOCALE_NAME_MAX_LENGTH, 0)) {
    char utf8[LOCALE_NAME_MAX_LENGTH * 4] = {};
    int count = WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, sizeof(utf8), nullptr, nullptr);
    if (count > 1) return normalizeLocale(utf8);
  }
#elif defined(__APPLE__)
  CFArrayRef languages = CFLocaleCopyPreferredLanguages();
  if (languages && CFArrayGetCount(languages) > 0) {
    CFStringRef first = static_cast<CFStringRef>(CFArrayGetValueAtIndex(languages, 0));
    char utf8[128] = {};
    if (CFStringGetCString(first, utf8, sizeof(utf8), kCFStringEncodingUTF8)) {
      std::string result = normalizeLocale(utf8);
      CFRelease(languages);
      return result;
    }
  }
  if (languages) CFRelease(languages);
#else
  const char* raw = std::getenv("LC_ALL");
  if (!raw || !*raw) raw = std::getenv("LC_MESSAGES");
  if (!raw || !*raw) raw = std::getenv("LANG");
  if (raw && *raw) return normalizeLocale(raw);
#endif
  return "en-US";
}

const char* localeValue(const char* locale, const char* key) {
  const LocaleRow* first = std::begin(kLocaleRows);
  const LocaleRow* last = std::end(kLocaleRows);
  const LocaleRow* found = std::lower_bound(first, last, std::pair<const char*, const char*>{locale, key},
      [](const LocaleRow& row, const std::pair<const char*, const char*>& value) {
        int localeOrder = std::strcmp(row.locale, value.first);
        return localeOrder < 0 || (localeOrder == 0 && std::strcmp(row.key, value.second) < 0);
      });
  if (found != last && std::strcmp(found->locale, locale) == 0 && std::strcmp(found->key, key) == 0)
    return found->value;
  return nullptr;
}

const std::string& selectedLocale() {
  static const std::string selected = detectLocale();
  return selected;
}

}  // namespace

Language language() {
  const std::string& selected = selectedLocale();
  if (selected == "zh-Hant") return Language::TraditionalChinese;
  if (selected == "zh-Hans") return Language::SimplifiedChinese;
  return Language::English;
}

const char* translate(const char* key) {
  if (!key) return "";
  const std::string& selected = selectedLocale();
  if (const char* value = localeValue(selected.c_str(), key)) return value;
  const size_t dash = selected.find('-');
  if (dash != std::string::npos) {
    const std::string base = selected.substr(0, dash);
    if (const char* value = localeValue(base.c_str(), key)) return value;
  }
  if (const char* value = localeValue("en-US", key)) return value;
  return key;
}

}  // namespace zh
