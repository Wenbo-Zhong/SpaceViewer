#pragma once

#include "types.hpp"

#include <string>

bool writeCsv(const TreeSnapshot& tree, const Filter& filter, uint32_t root, const std::string& path,
              std::string& error);

bool writeSnapshot(const TreeSnapshot& tree, const std::string& path, std::string& error);
bool readSnapshot(const std::string& path, TreeSnapshot& tree, std::string& error);
