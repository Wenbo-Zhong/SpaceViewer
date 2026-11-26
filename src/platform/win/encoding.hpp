#pragma once

#include <string>

std::wstring utf8ToWide(const std::string& text);
std::string wideToUtf8(const std::wstring& text);
