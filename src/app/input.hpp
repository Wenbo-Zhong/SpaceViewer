#pragma once

#include <string>

// 宿主将系统事件转换为共享输入；窗口尺寸和鼠标坐标均使用物理像素。
struct Input {
  int windowW = 0;
  int windowH = 0;
  float dpi = 96.f;
#if defined(__linux__)
  // Linux 的内容密度与桌面控件尺寸可能不同，控件优先使用独立的 chromeDpi。
  float chromeDpi = 0.f;
#endif
  float controlDpi() const {
#if defined(__linux__)
    if (chromeDpi > 0.f) return chromeDpi;
#endif
    return dpi;
  }
  int mouseX = 0;
  int mouseY = 0;
  int command = -1;
  bool leftPressed = false;
  bool rightPressed = false;
  bool keyBackspace = false;
  bool keyDelete = false;
  bool keyEnter = false;
  bool keyTab = false;
  bool keyEsc = false;
  bool keyLeft = false;
  bool keyRight = false;
  bool keyHome = false;
  bool keyEnd = false;
  bool keyF5 = false;
  bool keyUpNav = false;
  bool keyBackNav = false;
  bool keyForwardNav = false;
  bool modCtrl = false;
  bool modShift = false;
  bool modAlt = false;
  bool copy = false;
  bool paste = false;
  bool selectAll = false;
  bool openShortcut = false;
  std::string typed;
  double timeSec = 0;

  // 每次 update 后消费一次性事件，保留窗口尺寸、鼠标位置和修饰键等持续状态。
  void clearEdges();
};
