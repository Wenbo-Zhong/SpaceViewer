#pragma once

#include "types.hpp"

#include <string>
#include <string_view>
#include <vector>

std::string formatSize(uint64_t bytes);
std::string formatCount(uint64_t n);
bool parseSize(const std::string& text, uint64_t& out);
std::string formatPercent(uint64_t part, uint64_t whole);

std::vector<std::string> splitList(const std::string& text);
bool globMatch(std::string_view text, std::string_view pattern);
bool passNameFilter(std::string_view name, const Filter& filter);

uint8_t categoryFromName(std::string_view name);
uint32_t categoryColor(uint8_t category);
uint32_t shadeColor(uint32_t rgb, int delta);
uint32_t mixColor(uint32_t a, uint32_t b, int tNum, int tDen);

std::string_view nodeName(const TreeSnapshot& tree, uint32_t index);
uint32_t addNode(TreeSnapshot& tree, uint32_t parent, const std::string& name, uint8_t flags,
                 uint8_t category, uint64_t size);
void accountBytes(TreeSnapshot& tree, uint32_t dir, uint64_t bytes);
std::string nodePath(const TreeSnapshot& tree, uint32_t index);

std::vector<uint64_t> computeVisible(const TreeSnapshot& tree, const Filter& filter);

size_t utf8Next(const std::string& s, size_t i);
size_t utf8Prev(const std::string& s, size_t i);

#ifdef _WIN32
std::wstring utf8ToWide(const std::string& text);
std::string wideToUtf8(const std::wstring& text);
#endif
