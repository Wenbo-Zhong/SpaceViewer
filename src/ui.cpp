#include "app.hpp"

#include "export.hpp"
#include "layout.hpp"
#include "motion.hpp"
#include "opencl_fill.hpp"
#include "text.hpp"
#include "util.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>

namespace {

int scaled(float dpi, int value) {
  return static_cast<int>(std::lround(static_cast<double>(value) * static_cast<double>(dpi) / 96.0));
}

bool inside(const HotRect& r, int x, int y) {
  return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

int hitHot(const std::vector<HotRect>& hots, int x, int y) {
  for (int i = static_cast<int>(hots.size()) - 1; i >= 0; --i) {
    if (inside(hots[static_cast<size_t>(i)], x, y)) return i;
  }
  return -1;
}

void fillRect(Frame& frame, int x, int y, int w, int h, uint32_t rgb) {
  if (w <= 0 || h <= 0) return;
  int x0 = std::max(0, x);
  int y0 = std::max(0, y);
  int x1 = std::min(frame.width, x + w);
  int y1 = std::min(frame.height, y + h);
  if (x0 >= x1 || y0 >= y1) return;
  uint32_t pixel = packRgb(rgb);
  for (int yy = y0; yy < y1; ++yy) {
    uint32_t* row = frame.bgra.data() + static_cast<size_t>(yy) * static_cast<size_t>(frame.width);
    for (int xx = x0; xx < x1; ++xx) row[xx] = pixel;
  }
}

void fillGradient(Frame& frame, int x, int y, int w, int h, uint32_t top, uint32_t bottom) {
  if (w <= 0 || h <= 0) return;
  int x0 = std::max(0, x);
  int y0 = std::max(0, y);
  int x1 = std::min(frame.width, x + w);
  int y1 = std::min(frame.height, y + h);
  if (x0 >= x1 || y0 >= y1) return;
  int span = std::max(1, h - 1);
  for (int yy = y0; yy < y1; ++yy) {
    int t = yy - y;
    if (t < 0) t = 0;
    if (t > span) t = span;
    uint32_t pixel = packRgb(mixColor(top, bottom, t, span));
    uint32_t* row = frame.bgra.data() + static_cast<size_t>(yy) * static_cast<size_t>(frame.width);
    for (int xx = x0; xx < x1; ++xx) row[xx] = pixel;
  }
}

void strokeRect(Frame& frame, int x, int y, int w, int h, uint32_t rgb, int thickness) {
  fillRect(frame, x, y, w, thickness, rgb);
  fillRect(frame, x, y + h - thickness, w, thickness, rgb);
  fillRect(frame, x, y, thickness, h, rgb);
  fillRect(frame, x + w - thickness, y, thickness, h, rgb);
}

void drawText(Frame& frame, int x, int y, int w, int h, const std::string& text, uint32_t rgb, int fontPx,
              bool center) {
  if (w <= 2 || h <= 2 || text.empty()) return;
  Frame::Text item;
  item.x = x;
  item.y = y;
  item.w = w;
  item.h = h;
  item.rgb = rgb;
  item.fontPx = fontPx;
  item.center = center;
  item.utf8 = text;
  frame.texts.push_back(std::move(item));
}

int iFloor(double value) { return static_cast<int>(std::floor(value)); }

enum class Focus { None, Path, Inc, Exc, Min };

struct Edit {
  std::string text;
  size_t cursor = 0;
  bool selectAll = false;
};

void insertText(Edit& edit, const std::string& text) {
  if (edit.selectAll) {
    edit.text.clear();
    edit.cursor = 0;
    edit.selectAll = false;
  }
  if (edit.cursor > edit.text.size()) edit.cursor = edit.text.size();
  edit.text.insert(edit.cursor, text);
  edit.cursor += text.size();
}

void editKey(Edit& edit, const Input& input) {
  if (input.selectAll) {
    edit.selectAll = true;
    return;
  }
  if (input.copy) return;
  if (input.paste) return;
  if (input.keyHome) {
    edit.cursor = 0;
    edit.selectAll = false;
    return;
  }
  if (input.keyEnd) {
    edit.cursor = edit.text.size();
    edit.selectAll = false;
    return;
  }
  if (input.keyLeft) {
    edit.cursor = utf8Prev(edit.text, edit.cursor);
    edit.selectAll = false;
    return;
  }
  if (input.keyRight) {
    edit.cursor = utf8Next(edit.text, edit.cursor);
    edit.selectAll = false;
    return;
  }
  if (input.keyBackspace) {
    if (edit.selectAll) {
      edit.text.clear();
      edit.cursor = 0;
      edit.selectAll = false;
      return;
    }
    size_t prev = utf8Prev(edit.text, edit.cursor);
    if (prev != edit.cursor) edit.text.erase(prev, edit.cursor - prev);
    edit.cursor = prev;
    return;
  }
  if (input.keyDelete) {
    if (edit.selectAll) {
      edit.text.clear();
      edit.cursor = 0;
      edit.selectAll = false;
      return;
    }
    size_t next = utf8Next(edit.text, edit.cursor);
    if (next != edit.cursor) edit.text.erase(edit.cursor, next - edit.cursor);
    return;
  }
  if (!input.typed.empty()) insertText(edit, input.typed);
}

std::string tileLabel(const TreeSnapshot& tree, const Tile& tile) {
  if (tile.mergeCount > 0) {
    return std::string("\u5176\u4f59 ") + formatCount(tile.mergeCount) + " \u9879";
  }
  if (tile.index == kNone || tile.index >= tree.nodes.size()) return {};
  if (tile.flags & kPending) return tree.complete ? std::string(zh::uncounted()) : std::string(zh::scanningRegion());
  if (tile.flags & kFree) return zh::freeSpace();
  std::string name(nodeName(tree, tile.index));
  return name;
}

}  // namespace

struct App::State {
  Scanner scanner;
  std::function<void()> wake;
  std::mutex noteMu;
  std::thread exportThread;
  std::atomic<bool> exportRunning{false};
  std::string note;
  double noteUntil = 0;

  Edit path;
  Edit inc;
  Edit exc;
  Edit min;
  bool minInvalid = false;
  Focus focus = Focus::None;
  Filter filter;
  std::vector<HotRect> hots;
  std::vector<Tile> tiles;
  TileMotion motion;
  bool frameAnim = false;
  uint64_t seenChange = 0;
  double flashUntil = 0;
  std::vector<uint32_t> flashing;
  std::vector<uint32_t> back;
  std::vector<uint32_t> forward;
  std::vector<VolumeEntry> drives;
  bool driveOpen = false;
  uint32_t view = 0;
  uint32_t selected = kNone;
  int menuIndex = -1;
  bool menuPending = false;
  MenuRequest menu;
  std::string lastTitle;
  double lastClick = -1;
  int lastX = 0;
  int lastY = 0;
  bool lastZoom = false;
  std::shared_ptr<const TreeSnapshot> shown;
  std::vector<uint64_t> visible;
  const TreeSnapshot* visibleKey = nullptr;
  std::string visibleInc;
  std::string visibleExc;
  uint64_t visibleMin = 0;

  ~State() {
    if (exportThread.joinable()) exportThread.join();
  }

  void setNote(const std::string& text, double now) {
    std::lock_guard<std::mutex> lock(noteMu);
    note = text;
    noteUntil = now + 4.0;
  }

  std::string currentNote(double now) {
    std::lock_guard<std::mutex> lock(noteMu);
    if (now > noteUntil) return {};
    return note;
  }

  void scan(const std::string& pathText) {
    if (pathText.empty()) return;
    back.clear();
    forward.clear();
    view = 0;
    selected = kNone;
    driveOpen = false;
    focus = Focus::None;
    path.text = pathText;
    path.cursor = path.text.size();
    scanner.start(pathText);
  }

  Edit* focusedEdit() {
    if (focus == Focus::Path) return &path;
    if (focus == Focus::Inc) return &inc;
    if (focus == Focus::Exc) return &exc;
    if (focus == Focus::Min) return &min;
    return nullptr;
  }

  void refreshFilter() {
    filter.includeGlobs = splitList(inc.text);
    filter.excludeGlobs = splitList(exc.text);
    uint64_t parsed = 0;
    if (min.text.empty()) {
      filter.minSize = 0;
      minInvalid = false;
    } else if (parseSize(min.text, parsed)) {
      filter.minSize = parsed;
      minInvalid = false;
    } else {
      minInvalid = true;
    }
  }

  void navigate(uint32_t index) {
    if (index == view) return;
    forward.clear();
    back.push_back(view);
    if (back.size() > 64) back.erase(back.begin());
    view = index;
    selected = index;
  }

  std::string pathOf(uint32_t index) const {
    if (!shown || index >= shown->nodes.size()) return {};
    return nodePath(*shown, index);
  }

  void copyIndex(uint32_t index, const Host& host, double now) {
    std::string pathText = pathOf(index);
    if (pathText.empty() || !host.setClipboard) return;
    host.setClipboard(pathText);
    setNote(zh::copied(), now);
  }
};

App::App() : state(new State) {}

App::~App() { delete state; }

void App::setWake(std::function<void()> wake) {
  state->wake = wake;
  state->scanner.setWake(wake);
}

void App::scanPath(const std::string& path) { state->scan(path); }

bool App::animating() const { return state->frameAnim; }

bool App::ibeam(int x, int y) const {
  int index = hitHot(state->hots, x, y);
  if (index < 0) return false;
  HotKind kind = state->hots[static_cast<size_t>(index)].kind;
  return kind == HotKind::EditPath || kind == HotKind::EditInc || kind == HotKind::EditExc ||
         kind == HotKind::EditMin;
}

bool App::takeMenu(MenuRequest& out) {
  if (!state->menuPending) return false;
  state->menuPending = false;
  out = state->menu;
  return true;
}

void App::applyMenu(MenuCommand command, const Host& host) {
  if (state->menuIndex < 0 || state->menuIndex >= static_cast<int>(state->tiles.size())) return;
  const Tile& tile = state->tiles[static_cast<size_t>(state->menuIndex)];
  if (tile.index == kNone) return;
  if (command == MenuCommand::Open && host.openPath) {
    std::string path = state->pathOf(tile.index);
    if (!path.empty()) host.openPath(path);
  } else if (command == MenuCommand::Reveal && host.revealPath) {
    std::string path = state->pathOf(tile.index);
    if (!path.empty()) host.revealPath(path);
  } else if (command == MenuCommand::CopyPath) {
    state->copyIndex(tile.index, host, 0);
    state->noteUntil = 4;
  } else if (command == MenuCommand::Zoom) {
    state->navigate(tile.index);
  }
}

void App::update(const Input& input, const Host& host, Frame& frame) {
  State& s = *state;
  if (input.paste && host.getClipboard) {
    if (Edit* edit = s.focusedEdit()) insertText(*edit, host.getClipboard());
  }
  if (Edit* edit = s.focusedEdit()) {
    if (input.copy && host.setClipboard) {
      host.setClipboard(edit->text);
      s.setNote(zh::copied(), input.timeSec);
    }
    editKey(*edit, input);
    if (input.keyTab) {
      s.focus = static_cast<Focus>((static_cast<int>(s.focus) % 4) + 1);
    }
    if (input.keyEsc) s.focus = Focus::None;
    if (input.keyEnter && s.focus == Focus::Path) s.scan(s.path.text);
    if (input.keyEnter && s.focus == Focus::Min) s.focus = Focus::None;
  } else {
    if (input.keyBackspace || input.keyUpNav) {
      if (s.view != 0 && s.shown && s.view < s.shown->nodes.size()) s.navigate(s.shown->nodes[s.view].parent);
    } else if (input.keyBackNav && !s.back.empty()) {
      s.forward.push_back(s.view);
      s.view = s.back.back();
      s.back.pop_back();
    } else if (input.keyForwardNav && !s.forward.empty()) {
      s.back.push_back(s.view);
      s.view = s.forward.back();
      s.forward.pop_back();
    } else if (input.keyEsc) {
      if (s.driveOpen) s.driveOpen = false;
      else if (s.scanner.running()) s.scanner.requestStop();
      else if (s.view != 0 && s.shown && s.view < s.shown->nodes.size()) s.navigate(s.shown->nodes[s.view].parent);
    } else if (input.keyEnter && s.selected != kNone && s.shown && s.selected < s.shown->nodes.size()) {
      const Node& node = s.shown->nodes[s.selected];
      if ((node.flags & kDir) && !(node.flags & (kFree | kPending))) s.navigate(s.selected);
      else if (host.openPath) {
        std::string path = s.pathOf(s.selected);
        if (!path.empty()) host.openPath(path);
      }
    } else if (input.copy && s.selected != kNone) {
      s.copyIndex(s.selected, host, input.timeSec);
    }
  }
  if (input.keyF5) {
    std::string again = s.path.text;
    if (s.shown && s.view < s.shown->nodes.size()) {
      std::string current = nodePath(*s.shown, s.view);
      if (!current.empty()) again = current;
    }
    s.scan(again);
  }
  if (input.openShortcut && host.pickFolder) {
    std::string picked;
    if (host.pickFolder(picked)) s.scan(picked);
  }

  if (input.leftPressed || input.rightPressed) {
    int index = hitHot(s.hots, input.mouseX, input.mouseY);
    const HotRect* hot = index >= 0 ? &s.hots[static_cast<size_t>(index)] : nullptr;
    bool onPopup = hot && (hot->kind == HotKind::DriveItem || hot->kind == HotKind::Drives);
    if (!onPopup) s.driveOpen = false;
    if (input.leftPressed) {
      double previousClick = s.lastClick;
      int previousX = s.lastX;
      int previousY = s.lastY;
      uint32_t previousSelected = s.selected;
      bool second = s.lastZoom && (input.timeSec - s.lastClick) < 0.35 && std::abs(input.mouseX - s.lastX) < 6 &&
                    std::abs(input.mouseY - s.lastY) < 6;
      s.lastClick = input.timeSec;
      s.lastX = input.mouseX;
      s.lastY = input.mouseY;
      if (second) {
        s.lastZoom = false;
      } else if (!hot) {
        s.focus = Focus::None;
        s.selected = kNone;
        s.lastZoom = false;
      } else {
        switch (hot->kind) {
          case HotKind::Open:
            if (host.pickFolder) {
              std::string picked;
              if (host.pickFolder(picked)) s.scan(picked);
            }
            break;
          case HotKind::Drives:
            if (s.driveOpen) {
              s.driveOpen = false;
            } else if (host.volumes) {
              s.drives = host.volumes();
              s.driveOpen = true;
            }
            break;
          case HotKind::Back:
            if (!s.back.empty()) {
              s.forward.push_back(s.view);
              s.view = s.back.back();
              s.back.pop_back();
            }
            break;
          case HotKind::Forward:
            if (!s.forward.empty()) {
              s.back.push_back(s.view);
              s.view = s.forward.back();
              s.forward.pop_back();
            }
            break;
          case HotKind::Up:
            if (s.shown && s.view != 0 && s.view < s.shown->nodes.size()) s.navigate(s.shown->nodes[s.view].parent);
            break;
          case HotKind::Rescan: {
            std::string again = s.path.text;
            if (s.shown && s.view < s.shown->nodes.size()) {
              std::string current = nodePath(*s.shown, s.view);
              if (!current.empty()) again = current;
            }
            s.scan(again);
            break;
          }
          case HotKind::Stop:
            s.scanner.requestStop();
            break;
          case HotKind::Export:
            if (host.pickSave && s.shown && !s.shown->nodes.empty() && !s.exportRunning) {
              std::string dest;
              if (host.pickSave(dest)) {
                if (s.exportThread.joinable()) s.exportThread.join();
                auto snap = s.shown;
                Filter filter = s.filter;
                uint32_t root = s.view;
                s.exportRunning = true;
                s.setNote(zh::exporting(), input.timeSec);
                std::function<void()> wake = s.wake;
                s.exportThread = std::thread([&s, snap, filter, root, dest, wake]() {
                  std::string error;
                  bool ok = writeCsv(*snap, filter, root, dest, error);
                  {
                    std::lock_guard<std::mutex> lock(s.noteMu);
                    s.exportRunning = false;
                    s.note = ok ? (std::string(zh::exportOk()) + " " + dest) : error;
                    s.noteUntil = 1e12;
                  }
                  if (wake) wake();
                });
              }
            }
            break;
          case HotKind::EditPath:
          case HotKind::EditInc:
          case HotKind::EditExc:
          case HotKind::EditMin:
            s.focus = hot->kind == HotKind::EditPath   ? Focus::Path
                      : hot->kind == HotKind::EditInc ? Focus::Inc
                      : hot->kind == HotKind::EditExc ? Focus::Exc
                                                       : Focus::Min;
            if (Edit* edit = s.focusedEdit()) {
              edit->cursor = edit->text.size();
              edit->selectAll = false;
            }
            break;
          case HotKind::DriveItem:
            if (hot->driveIndex >= 0 && hot->driveIndex < static_cast<int>(s.drives.size())) {
              s.scan(s.drives[static_cast<size_t>(hot->driveIndex)].path);
            }
            break;
          case HotKind::Tile:
            if (hot->index == kNone || !s.shown || hot->index >= s.shown->nodes.size()) {
              s.setNote(zh::mergedNote(), input.timeSec);
              s.lastZoom = false;
            } else {
              const Node& node = s.shown->nodes[hot->index];
              s.selected = hot->index;
              bool zoom = (node.flags & kDir) && !(node.flags & (kFree | kPending));
              if (zoom) {
                s.navigate(hot->index);
                s.lastZoom = true;
              } else {
                s.lastZoom = false;
                bool again = previousSelected == hot->index && (input.timeSec - previousClick) < 0.35 &&
                             std::abs(input.mouseX - previousX) < 6 && std::abs(input.mouseY - previousY) < 6;
                if (again && host.openPath && !(node.flags & (kFree | kPending))) {
                  std::string path = s.pathOf(hot->index);
                  if (!path.empty()) host.openPath(path);
                }
              }
            }
            break;
        }
      }
    }
    if (input.rightPressed && hot && hot->kind == HotKind::Tile && hot->index != kNone && s.shown &&
        hot->index < s.shown->nodes.size()) {
      s.selected = hot->index;
      const Node& node = s.shown->nodes[hot->index];
      bool synthetic = (node.flags & (kFree | kPending)) != 0;
      bool dir = (node.flags & kDir) && !synthetic;
      s.menu = MenuRequest{};
      s.menu.x = input.mouseX;
      s.menu.y = input.mouseY;
      s.menu.canOpen = !synthetic;
      s.menu.canReveal = !synthetic;
      s.menu.canCopy = !synthetic;
      s.menu.canZoom = dir;
      s.menuPending = true;
      for (int i = 0; i < static_cast<int>(s.tiles.size()); ++i) {
        if (s.tiles[static_cast<size_t>(i)].index == hot->index) s.menuIndex = i;
      }
    }
  }

  s.refreshFilter();
  s.shown = s.scanner.snapshot();
  const TreeSnapshot* key = s.shown.get();
  if (key != s.visibleKey || s.inc.text != s.visibleInc || s.exc.text != s.visibleExc ||
      s.filter.minSize != s.visibleMin) {
    s.visible = s.shown ? computeVisible(*s.shown, s.filter) : std::vector<uint64_t>{};
    s.visibleKey = key;
    s.visibleInc = s.inc.text;
    s.visibleExc = s.exc.text;
    s.visibleMin = s.filter.minSize;
  }
  if (!s.shown || s.shown->nodes.empty()) s.view = 0;
  else if (s.view >= s.shown->nodes.size()) s.view = 0;
  if (s.focus != Focus::Path && s.shown && !s.shown->nodes.empty()) {
    std::string current = nodePath(*s.shown, s.view);
    if (!current.empty() && current != s.path.text) {
      s.path.text = current;
      s.path.cursor = s.path.text.size();
    }
  }

  const int width = std::max(1, input.windowW);
  const int height = std::max(1, input.windowH);
  frame.resize(width, height, 0xf3f5f7);
  const int gap = scaled(input.dpi, 6);
  const int row1 = scaled(input.dpi, 34);
  const int row2 = scaled(input.dpi, 30);
  const int statusH = scaled(input.dpi, 26);
  const int top = gap + row1 + gap + row2 + gap;
  const int font = std::max(11, scaled(input.dpi, 13));
  const int titlePx = std::max(12.0, static_cast<double>(scaled(input.dpi, 16)));
  fillRect(frame, 0, 0, width, top, 0xf7f8fa);
  fillRect(frame, 0, height - statusH, width, statusH, 0xe7ebf0);
  fillRect(frame, 0, top, width, 1, 0xd0d7de);
  fillRect(frame, 0, height - statusH, width, 1, 0xd0d7de);

  std::vector<HotRect> next;
  auto addHot = [&](int x, int y, int w, int h, HotKind kind, uint32_t index = kNone, int drive = -1) {
    HotRect hot;
    hot.x = x;
    hot.y = y;
    hot.w = w;
    hot.h = h;
    hot.kind = kind;
    hot.index = index;
    hot.driveIndex = drive;
    next.push_back(hot);
  };
  auto button = [&](int& x, int y, int w, int h, const char* label, HotKind kind, bool enabled) {
    bool hover = enabled && input.mouseX >= x && input.mouseY >= y && input.mouseX < x + w && input.mouseY < y + h;
    uint32_t bg = !enabled ? 0xeef1f4 : hover ? 0xd6e4f5 : 0xe7edf3;
    fillRect(frame, x, y, w, h, bg);
    strokeRect(frame, x, y, w, h, 0xc5ced6, 1);
    drawText(frame, x + 2, y, w - 4, h, label, enabled ? 0x1f2933 : 0x90a4ae, font, true);
    if (enabled) addHot(x, y, w, h, kind);
    x += w + gap;
  };

  int x = gap;
  int y = gap;
  const bool running = s.scanner.running();
  const bool hasTree = s.shown && !s.shown->nodes.empty();
  button(x, y, scaled(input.dpi, 64), row1, zh::open(), HotKind::Open, true);
  int driveX = x;
  button(x, y, scaled(input.dpi, 64), row1, zh::drives(), HotKind::Drives, true);
  button(x, y, scaled(input.dpi, 64), row1, zh::back(), HotKind::Back, !s.back.empty());
  button(x, y, scaled(input.dpi, 64), row1, zh::forward(), HotKind::Forward, !s.forward.empty());
  button(x, y, scaled(input.dpi, 64), row1, zh::up(), HotKind::Up, hasTree && s.view != 0);
  button(x, y, scaled(input.dpi, 96), row1, zh::rescan(), HotKind::Rescan, !s.path.text.empty());
  button(x, y, scaled(input.dpi, 88), row1, s.scanner.stopRequested() ? zh::stopping() : zh::stop(), HotKind::Stop,
         running);
  button(x, y, scaled(input.dpi, 64), row1, zh::exportCsv(), HotKind::Export, hasTree && !s.exportRunning);
  int pathX = x;
  int pathW = std::max(scaled(input.dpi, 80), width - gap - pathX);
  uint32_t pathBorder = s.focus == Focus::Path ? 0x2e86c1 : 0xc5ced6;
  fillRect(frame, pathX, y, pathW, row1, 0xffffff);
  strokeRect(frame, pathX, y, pathW, row1, pathBorder, s.focus == Focus::Path ? 2 : 1);
  drawText(frame, pathX + scaled(input.dpi, 6), y, pathW - scaled(input.dpi, 12), row1, s.path.text, 0x1f2933, font,
           false);
  addHot(pathX, y, pathW, row1, HotKind::EditPath);

  auto field = [&](int& fx, int fy, int labelW, int boxW, const char* label, const Edit& edit, Focus which,
                   const char* hint, bool invalid, HotKind kind) {
    drawText(frame, fx, fy, labelW, row2, label, 0x34495e, font, false);
    fx += labelW;
    uint32_t border = invalid ? 0xc0392b : s.focus == which ? 0x2e86c1 : 0xc5ced6;
    fillRect(frame, fx, fy, boxW, row2, 0xffffff);
    strokeRect(frame, fx, fy, boxW, row2, border, s.focus == which || invalid ? 2 : 1);
    const std::string& shownText = edit.text.empty() && s.focus != which ? std::string(hint) : edit.text;
    uint32_t color = edit.text.empty() && s.focus != which ? 0x95a5a6 : 0x1f2933;
    drawText(frame, fx + 4, fy, boxW - 8, row2, shownText, color, font, false);
    addHot(fx, fy, boxW, row2, kind);
    fx += boxW + gap;
  };

  x = gap;
  y = gap + row1 + gap;
  int boxW = std::max(scaled(input.dpi, 120), (width - gap * 8) / 5);
  field(x, y, scaled(input.dpi, 42), boxW, zh::include(), s.inc, Focus::Inc, zh::hintInclude(), false, HotKind::EditInc);
  field(x, y, scaled(input.dpi, 42), boxW, zh::exclude(), s.exc, Focus::Exc, zh::hintExclude(), false, HotKind::EditExc);
  field(x, y, scaled(input.dpi, 42), scaled(input.dpi, 90), zh::minSize(), s.min, Focus::Min, zh::hintMin(),
        s.minInvalid, HotKind::EditMin);

  int mapX = gap;
  int mapY = top;
  int mapW = std::max(1, width - gap * 2);
  int mapH = std::max(1, height - statusH - top - gap);
  fillRect(frame, mapX, mapY, mapW, mapH, 0xdfe6ee);
  if (hasTree) {
    RectF area{static_cast<double>(mapX), static_cast<double>(mapY), static_cast<double>(mapW),
               static_cast<double>(mapH)};
    const int labelFont = std::max(9, font - 2);
    const double minW = std::max(56.0, static_cast<double>(labelFont) * 5.0);
    const double minH = static_cast<double>(labelFont) * 2.0 + 14.0;
    std::vector<Tile> laid = layoutTreemap(*s.shown, s.visible, s.view, area, titlePx, minW, minH);
    s.motion.setTarget(laid, input.timeSec);
    s.tiles = s.motion.sample(input.timeSec);
    s.frameAnim = s.motion.running(input.timeSec);
    if (s.shown->changeGen != s.seenChange) {
      s.seenChange = s.shown->changeGen;
      if (!s.shown->changed.empty()) {
        s.flashing = s.shown->changed;
        s.flashUntil = input.timeSec + 0.55;
      }
    }
    const bool flashing = input.timeSec < s.flashUntil;
    int dividerPx = std::max(1, scaled(input.dpi, 2));
    const bool gpu = openclFillTiles(frame, s.tiles, mapX, mapY, mapW, mapH, dividerPx);
    int hoverTile = -1;
    for (int i = 0; i < static_cast<int>(s.tiles.size()); ++i) {
      const Tile& tile = s.tiles[static_cast<size_t>(i)];
      int tx = iFloor(tile.outer.x);
      int ty = iFloor(tile.outer.y);
      int tw = iFloor(tile.outer.x + tile.outer.w) - tx;
      int th = iFloor(tile.outer.y + tile.outer.h) - ty;
      if (tw <= 0 || th <= 0) continue;
      if (input.mouseX >= tx && input.mouseY >= ty && input.mouseX < tx + tw && input.mouseY < ty + th) hoverTile = i;
      uint32_t base = categoryColor(tile.flags & kDir ? CatDir : tile.category);
      if (tile.flags & kFree) base = categoryColor(CatFree);
      if (tile.flags & kPending) base = 0xf5cba7;
      int shift = 0;
      if (tile.index != kNone && tile.index < s.shown->nodes.size()) {
        std::string_view name = nodeName(*s.shown, tile.index);
        uint32_t hash = 2166136261u;
        for (char c : name) hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
        shift = static_cast<int>(hash % 17) - 8;
      }
      uint32_t topColor = shadeColor(base, 16 + shift / 2);
      uint32_t bottomColor = shadeColor(base, -14 + shift / 2);
      uint32_t titleColor = shadeColor(base, -28);
      uint32_t dividerColor = shadeColor(base, -70);
      if (!gpu) {
        if (th < 8) fillRect(frame, tx, ty, tw, th, base);
        else fillGradient(frame, tx, ty, tw, th, topColor, bottomColor);
      }
      strokeRect(frame, tx, ty, tw, th, 0xffffff, 1);
      if (flashing && tile.index != kNone &&
          std::find(s.flashing.begin(), s.flashing.end(), tile.index) != s.flashing.end()) {
        strokeRect(frame, tx + 1, ty + 1, std::max(0, tw - 2), std::max(0, th - 2), 0xf7dc6f, 2);
      }
      std::string name = tileLabel(*s.shown, tile);
      std::string sizeText = formatSize(tile.size);
      if (tile.title.h >= font) {
        int hy = iFloor(tile.title.y);
        int hh = iFloor(tile.title.y + tile.title.h) - hy;
        int dh = std::min(dividerPx, std::max(1, hh / 5));
        int textH = hh - dh;
        if (!gpu) {
          fillRect(frame, tx, hy, tw, hh, titleColor);
          fillRect(frame, tx, hy + hh - dh, tw, dh, dividerColor);
        }
        if (textH >= font && tw >= font * 3) {
          std::string header = name;
          if (tw >= font * 8) header += "  " + sizeText;
          drawText(frame, tx + 4, hy, tw - 8, textH, header, 0x1b2631, font, true);
        }
      } else if (th >= labelFont * 2 + 8 && tw >= labelFont * 4) {
        int line = labelFont + 1;
        int block = line * 2;
        int y0 = ty + std::max(0, (th - block) / 2);
        drawText(frame, tx + 2, y0, tw - 4, line, name, 0x1b2631, labelFont, true);
        drawText(frame, tx + 2, y0 + line, tw - 4, line, sizeText, 0x1b2631, labelFont, true);
      }
      addHot(tx, ty, tw, th, HotKind::Tile, tile.mergeCount ? kNone : tile.index);
    }
    auto paintBorder = [&](int index, uint32_t color) {
      if (index < 0 || index >= static_cast<int>(s.tiles.size())) return;
      const Tile& tile = s.tiles[static_cast<size_t>(index)];
      int tx = iFloor(tile.outer.x);
      int ty = iFloor(tile.outer.y);
      int tw = iFloor(tile.outer.x + tile.outer.w) - tx;
      int th = iFloor(tile.outer.y + tile.outer.h) - ty;
      strokeRect(frame, tx, ty, tw, th, color, 2);
    };
    int selectedTile = -1;
    for (int i = 0; i < static_cast<int>(s.tiles.size()); ++i) {
      if (s.tiles[static_cast<size_t>(i)].index == s.selected && s.selected != kNone) selectedTile = i;
    }
    paintBorder(hoverTile, 0x1b4f72);
    paintBorder(selectedTile, 0xe67e22);
    if (s.tiles.empty()) {
      std::string message = s.filter.includeGlobs.empty() && s.filter.excludeGlobs.empty() && s.filter.minSize == 0
                                ? zh::emptyFolder()
                                : zh::noMatch();
      drawText(frame, mapX, mapY + mapH / 2 - font, mapW, font * 2, message, 0x34495e, font, true);
    }
  } else {
    s.tiles.clear();
    s.motion.setTarget({}, input.timeSec);
    s.frameAnim = s.motion.running(input.timeSec);
    std::string message = s.shown && !s.shown->error.empty() ? s.shown->error : zh::emptyHint();
    drawText(frame, mapX + gap, mapY + mapH / 2 - font, mapW - gap * 2, font * 3, message, 0x34495e, font, true);
  }

  std::string status;
  if (s.shown && hasTree) {
    status += zh::files();
    status += " ";
    status += formatCount(s.shown->files);
    status += "   ";
    status += zh::folders();
    status += " ";
    status += formatCount(s.shown->dirs);
    status += "   ";
    status += zh::used();
    status += " ";
    uint64_t used = s.shown->nodes[0].size;
    if (s.shown->pending != kNone && s.shown->pending < s.shown->nodes.size()) {
      uint64_t pending = s.shown->nodes[s.shown->pending].size;
      if (used >= pending) used -= pending;
    }
    status += formatSize(s.view < s.visible.size() ? s.visible[s.view] : used);
    if (s.shown->freeBytes) {
      status += "   ";
      status += zh::avail();
      status += " ";
      status += formatSize(s.shown->freeBytes);
    }
    if (s.shown->errors) {
      status += "   ";
      status += zh::errors();
      status += " ";
      status += formatCount(s.shown->errors);
    }
    status += "   ";
    if (running) {
      status += zh::scanning();
      if (s.shown->elapsed > 0.3 && s.shown->files > 0) {
        double rate = static_cast<double>(s.shown->files) / s.shown->elapsed;
        status += " ";
        status += formatCount(static_cast<uint64_t>(rate));
        status += " ";
        status += zh::perSec();
      }
    } else if (s.shown->aborted) {
      status += zh::stopped();
    } else if (s.shown->complete) {
      status += zh::scanDone();
      if (s.shown->watchGap) {
        status += "   ";
        status += zh::watchGap();
      }
    } else {
      status += zh::ready();
    }
  } else {
    status = zh::ready();
  }
  std::string extra = s.currentNote(input.timeSec);
  if (s.minInvalid) extra = zh::invalidMin();
  std::string detail;
  uint32_t detailIndex = s.selected;
  if (detailIndex == kNone) {
    int hover = hitHot(next, input.mouseX, input.mouseY);
    if (hover >= 0 && next[static_cast<size_t>(hover)].kind == HotKind::Tile) {
      detailIndex = next[static_cast<size_t>(hover)].index;
    }
  }
  if (s.shown && detailIndex != kNone && detailIndex < s.shown->nodes.size()) {
    const Node& node = s.shown->nodes[detailIndex];
    detail = std::string(nodeName(*s.shown, detailIndex));
    uint64_t size = detailIndex < s.visible.size() ? s.visible[detailIndex] : node.size;
    detail += "   ";
    detail += formatSize(size);
    uint64_t whole = s.view < s.visible.size() ? s.visible[s.view] : 0;
    detail += "   ";
    detail += zh::ofView();
    detail += " ";
    detail += formatPercent(size, whole);
    std::string pathText = nodePath(*s.shown, detailIndex);
    if (!pathText.empty()) {
      detail += "   ";
      detail += pathText;
    }
    if (node.flags & kPending && s.shown->complete) {
      detail += "   ";
      detail += zh::uncountedNote();
    }
  } else if (running && s.shown && !s.shown->tip.empty()) {
    detail = std::string(zh::scanning()) + "  " + s.shown->tip;
  }
  if (!extra.empty()) {
    if (!detail.empty()) detail += "   ";
    detail += extra;
  }
  drawText(frame, gap, height - statusH, width / 2 - gap, statusH, status, 0x1f2933, font, false);
  drawText(frame, width / 2, height - statusH, width / 2 - gap, statusH, detail, 0x1f2933, font, false);

  if (s.driveOpen) {
    int row = scaled(input.dpi, 28);
    int popupW = std::min(width - gap * 2, scaled(input.dpi, 460));
    int popupX = std::min(driveX, width - popupW - gap);
    int popupY = gap + row1;
    int popupH = row * static_cast<int>(s.drives.size());
    fillRect(frame, popupX, popupY, popupW, std::max(row, popupH), 0xffffff);
    strokeRect(frame, popupX, popupY, popupW, std::max(row, popupH), 0x95a5a6, 1);
    if (s.drives.empty()) {
      drawText(frame, popupX + 8, popupY, popupW - 16, row, zh::emptyFolder(), 0x7f8c8d, font, false);
    }
    for (int i = 0; i < static_cast<int>(s.drives.size()); ++i) {
      int ry = popupY + i * row;
      bool hover = input.mouseX >= popupX && input.mouseY >= ry && input.mouseX < popupX + popupW &&
                   input.mouseY < ry + row;
      if (hover) fillRect(frame, popupX + 1, ry, popupW - 2, row, 0xd6eaf8);
      const VolumeEntry& volume = s.drives[static_cast<size_t>(i)];
      std::string line = volume.label.empty() ? volume.path : volume.label;
      line += "    ";
      line += formatSize(volume.free);
      line += " / ";
      line += formatSize(volume.total);
      drawText(frame, popupX + 8, ry, popupW - 16, row, line, 0x1f2933, font, false);
      addHot(popupX, ry, popupW, row, HotKind::DriveItem, kNone, i);
    }
  }

  s.hots.swap(next);
  std::string title = "SpaceViewer";
  if (!s.path.text.empty()) {
    title += " - ";
    title += s.path.text;
  }
  if (title != s.lastTitle && host.setTitle) {
    s.lastTitle = title;
    host.setTitle(title);
  }
}
