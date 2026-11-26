#pragma once

#include "model/tree.hpp"

#include <functional>

struct Host {
  std::function<std::vector<VolumeEntry>()> volumes;
  std::function<bool(std::string& path)> pickFolder;
  std::function<bool(std::string& path)> pickSave;
  std::function<void(const std::string& path)> openPath;
  std::function<void(const std::string& path)> revealPath;
  std::function<void(const std::string& text)> setClipboard;
  std::function<std::string()> getClipboard;
  std::function<void(const std::string& title)> setTitle;
  std::function<void()> configure;
  std::function<void()> newWindow;
  std::function<void(uint64_t count)> permissionWarning;
};
