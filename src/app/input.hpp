#pragma once

#include <string>

struct Input {
  int windowW = 0;
  int windowH = 0;
  float dpi = 96.f;
#if defined(__linux__)
  // View density and desktop control sizes may differ on Linux.
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

  // Consume one-frame events without resetting persistent device state.
  void clearEdges();
};
