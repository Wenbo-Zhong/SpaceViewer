#include "menu.hpp"
#include "text.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
struct Item {
  const char* label;
  HotKind action;
  bool rootScan = false;
};

struct ContextItem { const char* label; MenuCommand action; };

std::vector<ContextItem> contextItems(const MenuRequest& request) {
  const char* reveal = zh::language() == zh::Language::English ? "Show in Files" :
      zh::language() == zh::Language::TraditionalChinese ? "在檔案管理員中顯示" : "在文件中显示";
  std::vector<ContextItem> items;
  if (request.canReveal) items.push_back({reveal, MenuCommand::Reveal});
  if (request.canOpen) items.push_back({zh::open(), MenuCommand::Open});
  if (request.canCopy) items.push_back({zh::copyPath(), MenuCommand::CopyPath});
  if (request.canZoom) items.push_back({zh::enterFolder(), MenuCommand::Zoom});
  return items;
}

int scale(float dpi, int value) {
  return std::max(1, static_cast<int>(std::lround(value * dpi / 96.0f)));
}

int titleHeight(float dpi) { return scale(dpi, 40); }
int barHeight(float dpi) { return scale(dpi, 28); }
int topHeight(float dpi) { return titleHeight(dpi) + barHeight(dpi); }

std::vector<Item> itemsFor(int menu) {
  if (menu == 0) return {{"选择文件夹...", HotKind::Open}, {"重新扫描", HotKind::Rescan},
                        {"导出 CSV...", HotKind::Export}};
  if (menu == 1) return {{"后退", HotKind::Back}, {"前进", HotKind::Forward}, {"向上", HotKind::Up},
                        {"扫描根目录", HotKind::Home}, {"以管理员权限扫描根目录", HotKind::Home, true},
                        {"减少细节", HotKind::LessDetail},
                        {"增加细节", HotKind::MoreDetail}, {"文件类型着色", HotKind::FileGroups},
                        {"显示可用空间", HotKind::ShowFree}, {"显示未知空间", HotKind::ShowUnknown}};
  return {};
}

int menuAt(int x, float dpi) {
  const int left = scale(dpi, 8);
  const int width = scale(dpi, 64);
  if (x >= left && x < left + width) return 0;
  if (x >= left + width && x < left + width * 2) return 1;
  return -1;
}

void fill(Frame& frame, int x, int y, int w, int h, uint32_t rgb) {
  const int x0 = std::max(0, x), y0 = std::max(0, y);
  const int x1 = std::min(frame.width, x + w), y1 = std::min(frame.height, y + h);
  const uint32_t pixel = packRgb(rgb);
  for (int yy = y0; yy < y1; ++yy)
    std::fill(frame.bgra.begin() + static_cast<size_t>(yy) * frame.width + x0,
              frame.bgra.begin() + static_cast<size_t>(yy) * frame.width + x1, pixel);
}

void circle(Frame& frame, int cx, int cy, int radius, uint32_t rgb) {
  const uint32_t pixel = packRgb(rgb);
  for (int y = -radius; y <= radius; ++y) {
    const int span = static_cast<int>(std::floor(std::sqrt(radius * radius - y * y)));
    const int y0 = cy + y;
    if (y0 < 0 || y0 >= frame.height) continue;
    const int x0 = std::max(0, cx - span), x1 = std::min(frame.width - 1, cx + span);
    for (int x = x0; x <= x1; ++x) frame.bgra[static_cast<size_t>(y0) * frame.width + x] = pixel;
  }
}

void drawControl(Frame& frame, int x, int y, int width, int height, int control, bool hovered, float dpi) {
  const int cx = x + width / 2, cy = y + height / 2;
  if (hovered) {
    circle(frame, cx, cy, scale(dpi, 14), control == 2 ? 0xc01c28 : 0xdeddda);
  }
  const uint32_t icon = hovered && control == 2 ? 0xffffff : 0x3d3846;
  const int t = std::max(1, scale(dpi, 1));
  if (control == 0) {
    fill(frame, cx - scale(dpi, 5), cy, scale(dpi, 10), t, icon);
  } else if (control == 1) {
    const int side = scale(dpi, 9);
    fill(frame, cx - side / 2, cy - side / 2, side, t, icon);
    fill(frame, cx - side / 2, cy + side / 2, side, t, icon);
    fill(frame, cx - side / 2, cy - side / 2, t, side, icon);
    fill(frame, cx + side / 2, cy - side / 2, t, side, icon);
  } else {
    const int d = scale(dpi, 4);
    for (int i = -d; i <= d; ++i) {
      fill(frame, cx + i, cy + i, t, t, icon);
      fill(frame, cx + i, cy - i, t, t, icon);
    }
  }
}

void text(Frame& frame, int x, int y, int w, int h, int font, const char* value, bool center = false) {
  Frame::Text item;
  item.x = x; item.y = y; item.w = w; item.h = h; item.fontPx = font;
  item.rgb = 0x263746; item.center = center; item.utf8 = value;
  frame.texts.push_back(std::move(item));
}

void coverTexts(Frame& frame, int x, int y, int w, int h) {
  // Native text is drawn after all pixel fills. Suppress covered labels so
  // file names cannot show through the menu background.
  frame.texts.erase(std::remove_if(frame.texts.begin(), frame.texts.end(), [&](const Frame::Text& item) {
    return item.x < x + w && item.x + item.w > x && item.y < y + h && item.y + item.h > y;
  }), frame.texts.end());
}
}

void linuxMenuPrepare(LinuxMenuState& menu, Input& input, bool clientTitlebar) {
  const int title = clientTitlebar ? titleHeight(input.controlDpi()) : 0;
  const int height = barHeight(input.controlDpi());
  const int top = title + height;
  menu.hoveredContextRow = -1;
  if (menu.contextOpen) {
    const auto items = contextItems(menu.context);
    const int rowH = scale(input.controlDpi(), 30);
    if (input.mouseX >= menu.contextX && input.mouseX < menu.contextX + menu.contextWidth &&
        input.mouseY >= menu.contextY && input.mouseY < menu.contextY + rowH * static_cast<int>(items.size()))
      menu.hoveredContextRow = (input.mouseY - menu.contextY) / rowH;
    if (input.keyEsc || input.rightPressed) {
      menu.contextOpen = false;
      input.keyEsc = false;
    } else if (input.leftPressed) {
      if (menu.hoveredContextRow >= 0)
        menu.contextCommand = items[static_cast<size_t>(menu.hoveredContextRow)].action;
      menu.contextOpen = false;
      input.leftPressed = false;
    }
  }
  menu.hoveredControl = clientTitlebar
      ? static_cast<int>(linuxTitlebarControlAt(input.mouseX, input.mouseY, input.windowW, input.controlDpi())) - 1
      : -1;
  if (input.leftPressed) {
    if (input.mouseY < title) {
      switch (linuxTitlebarControlAt(input.mouseX, input.mouseY, input.windowW, input.controlDpi())) {
        case LinuxWindowControl::Minimize: menu.minimizeRequested = true; break;
        case LinuxWindowControl::Maximize: menu.maximizeRequested = true; break;
        case LinuxWindowControl::Close: menu.closeRequested = true; break;
        case LinuxWindowControl::None: break;
      }
      input.leftPressed = false;
    } else if (input.mouseY < top) {
      menu.active = menuAt(input.mouseX, input.controlDpi());
      input.leftPressed = false;
    } else if (menu.active >= 0 && input.mouseY < top + scale(input.controlDpi(), 30) * 10) {
      const int row = (input.mouseY - top) / scale(input.controlDpi(), 30);
      const auto items = itemsFor(menu.active);
      if (row >= 0 && row < static_cast<int>(items.size())) {
        if (items[static_cast<size_t>(row)].rootScan) menu.rootScanRequested = true;
        else if (items[static_cast<size_t>(row)].action == HotKind::Open) input.openShortcut = true;
        else input.command = static_cast<int>(items[static_cast<size_t>(row)].action);
      }
      menu.active = -1;
      input.leftPressed = false;
    } else {
      menu.active = -1;
    }
  }
  input.mouseY -= top;
  input.windowH = std::max(1, input.windowH - top);
}

void linuxMenuShowContext(LinuxMenuState& menu, const MenuRequest& request, int width, int height,
                          float dpi, bool clientTitlebar) {
  menu.context = request;
  const auto items = contextItems(request);
  menu.contextOpen = !items.empty();
  menu.active = -1;
  menu.contextWidth = std::min(width, scale(dpi, 220));
  const int top = (clientTitlebar ? titleHeight(dpi) : 0) + barHeight(dpi);
  menu.contextX = std::clamp(request.x, 0, std::max(0, width - menu.contextWidth));
  menu.contextY = std::clamp(request.y + top, 0,
      std::max(0, height - scale(dpi, 30) * static_cast<int>(items.size())));
  menu.hoveredContextRow = -1;
}

void linuxMenuPresent(LinuxMenuState& menu, Frame& frame, int windowHeight, float dpi, bool clientTitlebar) {
  const int title = clientTitlebar ? titleHeight(dpi) : 0;
  const int height = barHeight(dpi);
  const int top = title + height;
  const int oldHeight = frame.height;
  std::vector<uint32_t> shifted(static_cast<size_t>(frame.width) * windowHeight, packRgb(0xf8fafc));
  for (int y = 0; y < oldHeight && y + top < windowHeight; ++y)
    std::copy_n(frame.bgra.begin() + static_cast<size_t>(y) * frame.width, frame.width,
                shifted.begin() + static_cast<size_t>(y + top) * frame.width);
  frame.height = windowHeight;
  frame.bgra = std::move(shifted);
  for (Frame::Text& item : frame.texts) item.y += top;

  if (clientTitlebar) {
    fill(frame, 0, 0, frame.width, title, 0xe8edf2);
    fill(frame, 0, title - 1, frame.width, 1, 0xc7d0d9);
    const int controlWidth = scale(dpi, 40);
    const int controlsX = std::max(0, frame.width - controlWidth * 3);
    text(frame, scale(dpi, 12), 0, controlsX - scale(dpi, 16), title - 1, scale(dpi, 13), "SpaceViewer");
    drawControl(frame, controlsX, 0, controlWidth, title, 0, menu.hoveredControl == 0, dpi);
    drawControl(frame, controlsX + controlWidth, 0, controlWidth, title, 1, menu.hoveredControl == 1, dpi);
    drawControl(frame, controlsX + controlWidth * 2, 0, controlWidth, title, 2, menu.hoveredControl == 2, dpi);
  }

  fill(frame, 0, title, frame.width, height, 0xf8fafc);
  fill(frame, 0, title + height - 1, frame.width, 1, 0xe6e8eb);
  const char* labels[] = {"文件", "视图"};
  const int left = scale(dpi, 8), width = scale(dpi, 64);
  for (int i = 0; i < 2; ++i) {
    if (menu.active == i) fill(frame, left + i * width, title + 1, width, height - 2, 0xe8edf2);
    text(frame, left + i * width, title, width, height - 1, scale(dpi, 13), labels[i], true);
  }
  if (menu.active >= 0) {
    const auto items = itemsFor(menu.active);
    if (items.empty()) return;
    const int rowH = scale(dpi, 30), popupW = scale(dpi, 190);
    const int x = left + menu.active * width;
    const int rows = static_cast<int>(items.size());
    coverTexts(frame, x, top, popupW, rowH * rows);
    fill(frame, x, top, popupW, rowH * rows, 0xffffff);
    fill(frame, x, top, popupW, 1, 0x9caab7);
    fill(frame, x, top + rowH * rows - 1, popupW, 1, 0x9caab7);
    fill(frame, x, top, 1, rowH * rows, 0x9caab7);
    fill(frame, x + popupW - 1, top, 1, rowH * rows, 0x9caab7);
    for (int i = 0; i < rows; ++i)
      text(frame, x + scale(dpi, 12), top + rowH * i, popupW - scale(dpi, 20), rowH,
           scale(dpi, 13), items[static_cast<size_t>(i)].label);
  }
  if (menu.contextOpen) {
    const auto items = contextItems(menu.context);
    const int x = menu.contextX, y = menu.contextY, w = menu.contextWidth;
    const int rowH = scale(dpi, 30), h = rowH * static_cast<int>(items.size());
    coverTexts(frame, x, y, w, h);
    fill(frame, x, y, w, h, 0x9caab7);
    fill(frame, x + 1, y + 1, w - 2, h - 2, 0xffffff);
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
      if (menu.hoveredContextRow == i) fill(frame, x + 1, y + i * rowH + 1, w - 2, rowH - 2, 0xe8edf2);
      text(frame, x + scale(dpi, 12), y + rowH * i, w - scale(dpi, 24), rowH,
           scale(dpi, 13), items[static_cast<size_t>(i)].label);
    }
  }
}

int linuxTitlebarHeight(float dpi) { return titleHeight(dpi); }

bool linuxTitlebarDragHit(int x, int y, int width, float dpi) {
  return y >= 0 && y < titleHeight(dpi) && x >= 0 &&
         x < width - scale(dpi, 40) * 3;
}

LinuxWindowControl linuxTitlebarControlAt(int x, int y, int width, float dpi) {
  if (y < 0 || y >= titleHeight(dpi)) return LinuxWindowControl::None;
  const int controlWidth = scale(dpi, 40);
  const int start = width - controlWidth * 3;
  if (x < start || x >= width) return LinuxWindowControl::None;
  const int slot = (x - start) / controlWidth;
  if (slot == 0) return LinuxWindowControl::Minimize;
  if (slot == 1) return LinuxWindowControl::Maximize;
  return LinuxWindowControl::Close;
}
