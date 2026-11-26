#pragma once

#include "app/host.hpp"
#include "app/input.hpp"
#include "view/frame.hpp"
#include "view/interaction.hpp"
#include "view/options.hpp"

#include <string>

// 跨平台应用核心，由宿主 UI 线程驱动；系统窗口、对话框和剪贴板通过 Host 接入。
class App {
 public:
  App();
  ~App();

  // 扫描和导出线程都可能触发此回调，宿主应将重绘请求调度到自己的 UI 线程。
  void setWake(std::function<void()> wake);
  void scanPath(const std::string& path);
  void requestStop();
  // 消费本帧输入并生成绘制数据；返回后宿主即可清理 Input 的一次性事件。
  void update(const Input& input, const Host& host, Frame& frame);
  bool animating() const;
  ViewOptions options() const;
  void setOptions(const ViewOptions& options);
  bool saveSnapshot(const std::string& path, std::string& error) const;
  bool loadSnapshot(const std::string& path, std::string& error);
  bool ibeam(int x, int y) const;
  bool takeMenu(MenuRequest& out);
  void applyMenu(MenuCommand command, const Host& host);

 private:
  struct State;
  State* state;
};
