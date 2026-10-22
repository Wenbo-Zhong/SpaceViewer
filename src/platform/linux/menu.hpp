#pragma once

#include "types.hpp"

struct LinuxMenuState {
  int active = -1;
  bool closeRequested = false;
  bool minimizeRequested = false;
  bool maximizeRequested = false;
  bool maximized = false;
  bool rootScanRequested = false;
  int hoveredControl = -1;
  bool contextOpen = false;
  MenuRequest context;
  int contextX = 0, contextY = 0, contextWidth = 0;
  int hoveredContextRow = -1;
  MenuCommand contextCommand = MenuCommand::None;
};

enum class LinuxWindowControl { None, Minimize, Maximize, Close };

// Reserve an in-window menu strip and route its clicks into existing app actions.
void linuxMenuPrepare(LinuxMenuState& menu, Input& input, bool clientTitlebar = true);
void linuxMenuPresent(LinuxMenuState& menu, Frame& frame, int windowHeight, float dpi, bool clientTitlebar = true);
void linuxMenuShowContext(LinuxMenuState& menu, const MenuRequest& request, int width, int height,
                          float dpi, bool clientTitlebar = true);
int linuxTitlebarHeight(float dpi);
bool linuxTitlebarDragHit(int x, int y, int width, float dpi);
LinuxWindowControl linuxTitlebarControlAt(int x, int y, int width, float dpi);
