#pragma once

#include "model/tree.hpp"

#include <functional>

// 原生能力的注入接口，回调由应用核心在 UI 线程调用；可选能力通过空回调表示。
// 路径与文本统一使用 UTF-8，系统编码转换留在各平台实现中。
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
