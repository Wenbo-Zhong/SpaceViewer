#pragma once

#include <cstdint>
#include <string>

std::string formatSize(uint64_t bytes);
std::string formatCount(uint64_t n);
bool parseSize(const std::string& text, uint64_t& out);
std::string formatPercent(uint64_t part, uint64_t whole);
