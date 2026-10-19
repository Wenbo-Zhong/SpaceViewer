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
#include <unordered_map>

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

void roundRect(Frame& frame, int x, int y, int w, int h, int radius, uint32_t color) {
  radius = std::max(0, std::min(radius, std::min(w, h) / 2));
  for (int row = 0; row < h; ++row) {
    int dy = row < radius ? radius - row - 1 : row >= h - radius ? row - (h - radius) : 0;
    int inset = dy ? radius - static_cast<int>(std::sqrt(radius * radius - dy * dy)) : 0;
    fillRect(frame, x + inset, y + row, w - inset * 2, 1, color);
  }
}

void toolbarIcon(Frame& frame, int x, int y, int size, HotKind kind, uint32_t ink, uint32_t tint) {
  auto line = [&](double ax, double ay, double bx, double by) {
    int steps = std::max(1, static_cast<int>(std::hypot(bx - ax, by - ay) * size));
    int weight = std::max(1, size / 13);
    for (int i = 0; i <= steps; ++i) {
      double t = static_cast<double>(i) / steps;
      fillRect(frame, x + static_cast<int>((ax + (bx - ax) * t) * size) - weight / 2,
               y + static_cast<int>((ay + (by - ay) * t) * size) - weight / 2, weight, weight, ink);
    }
  };
  auto box = [&](double a, double b, double w, double h) {
    line(a,b,a+w,b); line(a+w,b,a+w,b+h); line(a+w,b+h,a,b+h); line(a,b+h,a,b);
  };
  switch (kind) {
    case HotKind::Open:
      roundRect(frame,x+size/10,y+size/3,size*4/5,size/2,size/12,tint);
      line(.1,.8,.1,.22); line(.1,.22,.4,.22); line(.4,.22,.5,.34); line(.5,.34,.88,.34);
      line(.1,.8,.78,.8); line(.78,.8,.94,.42); line(.94,.42,.27,.42); line(.27,.42,.1,.8); break;
    case HotKind::Drives:
      roundRect(frame,x+size/8,y+size/5,size*3/4,size*3/5,size/10,tint);
      box(.13,.2,.74,.6); line(.13,.59,.87,.59); line(.66,.7,.73,.7); break;
    case HotKind::Back: case HotKind::Forward: {
      bool back = kind == HotKind::Back;
      double a = back ? .2 : .8, b = back ? .8 : .2;
      line(a,.5,b,.5); line(a,.5,.5,.2); line(a,.5,.5,.8); break;
    }
    case HotKind::Up:
      line(.5,.83,.5,.17); line(.5,.17,.2,.47); line(.5,.17,.8,.47); break;
    case HotKind::FocusRescan:
    case HotKind::Rescan:
      for (int i=0;i<32;++i) {
        double a=(35+i*9)*3.141592653589793/180, b=(35+(i+1)*9)*3.141592653589793/180;
        line(.5+.33*std::cos(a),.5+.33*std::sin(a),.5+.33*std::cos(b),.5+.33*std::sin(b));
      }
      line(.81,.18,.81,.42); line(.81,.42,.57,.42); break;
    case HotKind::Stop:
      roundRect(frame,x+size/4,y+size/4,size/2,size/2,size/12,ink); break;
    case HotKind::Export:
      line(.18,.51,.18,.84); line(.18,.84,.83,.84); line(.83,.84,.83,.51);
      line(.5,.65,.5,.12); line(.5,.12,.27,.35); line(.5,.12,.73,.35); break;
    case HotKind::Home:
      line(.12,.43,.5,.13); line(.5,.13,.88,.43); box(.24,.43,.52,.42); break;
    case HotKind::LessDetail: case HotKind::MoreDetail:
      box(.14,.14,.72,.72); line(.3,.5,.7,.5);
      if (kind == HotKind::MoreDetail) line(.5,.3,.5,.7); break;
    case HotKind::FileGroups:
      box(.12,.12,.34,.34); box(.54,.12,.34,.34); box(.12,.54,.34,.34); box(.54,.54,.34,.34); break;
    case HotKind::ShowFree:
      box(.15,.2,.7,.6); line(.5,.2,.5,.8); break;
    case HotKind::ShowUnknown:
      box(.15,.2,.7,.6); line(.38,.38,.6,.38); line(.6,.38,.5,.53); line(.5,.68,.5,.7); break;
    case HotKind::Configure:
      for (int i=0;i<3;++i) { double yy=.25+i*.25; line(.12,yy,.88,yy); box(.3+i*.12,yy-.08,.12,.16); } break;
    default: break;
  }
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

// Small tiles need useful prefixes, rather than an ellipsis consuming most
// of the available width. Keep complete UTF-8 code points, including CJK.
std::string compactTileText(const std::string& text, int width, int fontPx) {
  double used = 0;
  size_t end = 0;
  while (end < text.size()) {
    size_t next = utf8Next(text, end);
    unsigned char c = static_cast<unsigned char>(text[end]);
    double advance = fontPx * (c >= 128 ? 1.0 : c == 'i' || c == 'l' || c == '.' ? 0.3 : 0.58);
    if (used + advance > width - 1) break;
    used += advance;
    end = next;
  }
  return text.substr(0, end);
}

std::string tileLabel(const TreeSnapshot& tree, const Tile& tile) {
  if (tile.mergeCount > 0) {
    return std::string("\u5176\u4f59 ") + formatCount(tile.mergeCount) + " \u9879";
  }
  if (tile.flags & kFree) return zh::freeSpace();
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
  ViewOptions options;
  bool archived = false;
  uint64_t seenChange = 0;
  std::unordered_map<uint32_t, double> changes;
  uint64_t hovered = kNone;
  double hoverSince = 0;
  std::vector<uint32_t> back;
  std::vector<uint32_t> forward;
  std::vector<VolumeEntry> drives;
  bool driveOpen = false;
  uint32_t view = 0;
  std::vector<uint32_t> expanded;
  bool snapLayout = false;
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
    expanded.clear();
    changes.clear();
    seenChange = 0;
    hovered = kNone;
    view = 0;
    selected = kNone;
    driveOpen = false;
    focus = Focus::None;
    path.text = pathText;
    path.cursor = path.text.size();
    archived = false;
    if (globMatch(pathText, "*.svs")) {
      TreeSnapshot loaded;
      std::string error;
      if (readSnapshot(pathText, loaded, error)) { scanner.loadSnapshot(std::move(loaded)); archived = true; }
      else { std::lock_guard<std::mutex> lock(noteMu); note = error; noteUntil = 1e12; }
    } else scanner.start(pathText);
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

ViewOptions App::options() const { return state->options; }
void App::setOptions(const ViewOptions& options) {
  state->options = options;
  state->options.detail = std::max(0, std::min(6, options.detail));
  state->snapLayout = true;
}
bool App::saveSnapshot(const std::string& path, std::string& error) const {
  auto tree = state->scanner.snapshot();
  if (!tree || tree->nodes.empty()) { error = "No scan to save"; return false; }
  if (state->scanner.running()) { error = "Wait for the scan to finish before saving a snapshot"; return false; }
  return writeSnapshot(*tree, path, error);
}
bool App::loadSnapshot(const std::string& path, std::string& error) {
  TreeSnapshot tree;
  if (!readSnapshot(path, tree, error)) return false;
  state->back.clear(); state->forward.clear(); state->expanded.clear(); state->changes.clear();
  state->view = 0; state->selected = kNone; state->seenChange = 0;
  state->scanner.loadSnapshot(std::move(tree));
  state->archived = true; state->snapLayout = true;
  return true;
}

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
    if (host.newWindow && s.shown && !s.shown->nodes.empty()) host.newWindow();
    else { std::string picked; if (host.pickFolder(picked)) s.scan(picked); }
  }

  if (input.leftPressed || input.rightPressed || input.command >= 0) {
    int index = hitHot(s.hots, input.mouseX, input.mouseY);
    HotRect commandHot;
    if (input.command >= 0) commandHot.kind = static_cast<HotKind>(input.command);
    const HotRect* hot = input.command >= 0 ? &commandHot : index >= 0 ? &s.hots[static_cast<size_t>(index)] : nullptr;
    bool onPopup = hot && (hot->kind == HotKind::DriveItem || hot->kind == HotKind::Drives);
    if (!onPopup) s.driveOpen = false;
    if (input.leftPressed || input.command >= 0) {
      double previousClick = s.lastClick;
      int previousX = s.lastX;
      int previousY = s.lastY;
      uint32_t previousSelected = s.selected;
      bool second = input.command < 0 && s.lastZoom && (input.timeSec - s.lastClick) < 0.35 && std::abs(input.mouseX - s.lastX) < 6 &&
                    std::abs(input.mouseY - s.lastY) < 6;
      s.lastClick = input.timeSec;
      s.lastX = input.mouseX;
      s.lastY = input.mouseY;
      if (second) {
        // The first click may have revealed a child under the cursor. The
        // double click still belongs to the folder that received the first.
        if (s.shown && previousSelected < s.shown->nodes.size() &&
            (s.shown->nodes[previousSelected].flags & kDir)) s.navigate(previousSelected);
        s.lastZoom = false;
      } else if (!hot) {
        s.focus = Focus::None;
        s.selected = kNone;
        s.lastZoom = false;
      } else {
        switch (hot->kind) {
          case HotKind::Open:
            if (host.newWindow && s.shown && !s.shown->nodes.empty()) { host.newWindow(); break; }
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
          case HotKind::Home: s.navigate(0); break;
          case HotKind::FocusRescan: {
            uint32_t id = s.selected == kNone ? s.view : s.selected;
            if (s.shown && id < s.shown->nodes.size()) {
              if (!(s.shown->nodes[id].flags & kDir)) id = s.shown->nodes[id].parent;
              if (s.archived) { std::string path = s.pathOf(id); if (!path.empty()) s.scan(path); }
              else s.scanner.refreshSubtree(id);
            }
            break;
          }
          case HotKind::LessDetail:
            s.options.detail = std::max(0, s.options.detail - 1); s.expanded.clear(); s.snapLayout = true; break;
          case HotKind::MoreDetail:
            s.options.detail = std::min(6, s.options.detail + 1); s.snapLayout = true; break;
          case HotKind::FileGroups: s.options.fileGroups = !s.options.fileGroups; break;
          case HotKind::ShowFree: s.options.showFree = !s.options.showFree; s.snapLayout = true; break;
          case HotKind::ShowUnknown: s.options.showUnknown = !s.options.showUnknown; s.snapLayout = true; break;
          case HotKind::Configure: if (host.configure) host.configure(); break;
          case HotKind::CloseView:
            s.scanner.loadSnapshot(TreeSnapshot{}); s.view = 0; s.path.text.clear();
            s.path.cursor = 0; s.selected = kNone; s.expanded.clear(); break;
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
                s.expanded = expansionPath(*s.shown, s.view, hot->index);
                s.snapLayout = true;
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
  frame.resize(width, height, 0xf0f0f0);
  const int gap = scaled(input.dpi, 8);
  const int row1 = scaled(input.dpi, 64);
  const int row2 = scaled(input.dpi, 30);
  const int statusH = scaled(input.dpi, 26);
  const bool pathBelow = true;
  const int pathRow = pathBelow ? scaled(input.dpi, 32) + gap : 0;
  const int top = gap + row1 + gap + pathRow + row2 + gap;
  const int font = std::max(11, scaled(input.dpi, 13));
  const int titlePx = std::max(12.0, static_cast<double>(scaled(input.dpi, 14)));
  fillRect(frame, 0, 0, width, top, 0xf8fafc);
  fillRect(frame, 0, gap + row1 + gap / 2, width, 1, 0xe5eaf0);
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
  const double toolScale = std::min(1.0, static_cast<double>(std::max(1, width - gap * 16)) /
                                            scaled(input.dpi, 912));
  auto button = [&](int& x, int y, int w, int h, const char* label, HotKind kind, bool enabled) {
    w = std::max(1, static_cast<int>(w * toolScale));
    bool hover = enabled && input.mouseX >= x && input.mouseY >= y && input.mouseX < x + w && input.mouseY < y + h;
    const bool primary = kind == HotKind::Open;
    const bool checked = (kind == HotKind::FileGroups && s.options.fileGroups) ||
                         (kind == HotKind::ShowFree && s.options.showFree) ||
                         (kind == HotKind::ShowUnknown && s.options.showUnknown);
    uint32_t bg = hover ? 0xe5effa : (primary || checked) ? 0xeaf2fc : 0xf8fafc;
    roundRect(frame, x, y, w, h, scaled(input.dpi, 7), bg);
    uint32_t ink = !enabled ? 0xb6c0cc : kind == HotKind::Stop ? 0xc36b63 : 0x35658e;
    int iconSize = std::max(12, std::min(scaled(input.dpi, 24), w - 8));
    toolbarIcon(frame, x + (w - iconSize) / 2, y + scaled(input.dpi, 7), iconSize, kind,
                ink, enabled ? 0xd6e6f7 : 0xe9edf2);
    drawText(frame, x + 2, y + scaled(input.dpi, 36), w - 4, scaled(input.dpi, 24),
             label, enabled ? 0x34465b : 0xaab5c1, std::min(font, std::max(8, w / 4)), true);
    if (enabled) addHot(x, y, w, h, kind);
    x += w + gap;
  };

  int x = gap;
  int y = gap;
  const bool running = s.scanner.running();
  const bool hasTree = s.shown && !s.shown->nodes.empty();
  button(x, y, scaled(input.dpi, 64), row1, zh::open(), HotKind::Open, true);
  int driveX = x;
  button(x, y, scaled(input.dpi, 64), row1, zh::back(), HotKind::Back, !s.back.empty());
  button(x, y, scaled(input.dpi, 64), row1, zh::forward(), HotKind::Forward, !s.forward.empty());
  button(x, y, scaled(input.dpi, 64), row1, zh::up(), HotKind::Up, hasTree && s.view != 0);
  button(x, y, scaled(input.dpi, 56), row1, "\u6839\u76ee\u5f55", HotKind::Home, hasTree && s.view != 0);
  button(x, y, scaled(input.dpi, 80), row1, zh::rescan(), HotKind::Rescan, !s.path.text.empty());
  button(x, y, scaled(input.dpi, 64), row1, s.scanner.stopRequested() ? zh::stopping() : zh::stop(), HotKind::Stop,
         running);
  button(x, y, scaled(input.dpi, 64), row1, zh::exportCsv(), HotKind::Export, hasTree && !s.exportRunning);
  button(x, y, scaled(input.dpi, 56), row1, "\u5c40\u90e8\u5237\u65b0", HotKind::FocusRescan, hasTree && !running);
  button(x, y, scaled(input.dpi, 56), row1, "\u51cf\u5c11\u7ec6\u8282", HotKind::LessDetail, hasTree && s.options.detail > 0);
  button(x, y, scaled(input.dpi, 56), row1, "\u589e\u52a0\u7ec6\u8282", HotKind::MoreDetail, hasTree && s.options.detail < 6);
  button(x, y, scaled(input.dpi, 56), row1, "\u6587\u4ef6\u7c7b\u578b", HotKind::FileGroups, hasTree);
  button(x, y, scaled(input.dpi, 56), row1, "\u53ef\u7528\u7a7a\u95f4", HotKind::ShowFree, hasTree);
  button(x, y, scaled(input.dpi, 56), row1, "\u672a\u77e5\u7a7a\u95f4", HotKind::ShowUnknown, hasTree);
  button(x, y, scaled(input.dpi, 56), row1, "\u914d\u7f6e", HotKind::Configure, static_cast<bool>(host.configure));
  int pathX = pathBelow ? gap : x + gap;
  int pathY = pathBelow ? gap + row1 + gap : y + scaled(input.dpi, 25);
  int pathH = scaled(input.dpi, 32);
  int pathW = std::max(1, width - gap - pathX);
  if (!pathBelow) drawText(frame, pathX, y, pathW, scaled(input.dpi, 22),
                            "\u5f53\u524d\u8def\u5f84", 0x7c8b9c, font - 1, false);
  uint32_t pathBorder = s.focus == Focus::Path ? 0x6b9dcd : 0xdce3ec;
  roundRect(frame, pathX, pathY, pathW, pathH, scaled(input.dpi, 5), pathBorder);
  roundRect(frame, pathX + 1, pathY + 1, pathW - 2, pathH - 2, scaled(input.dpi, 4), 0xffffff);
  drawText(frame, pathX + scaled(input.dpi, 10), pathY, pathW - scaled(input.dpi, 20), pathH,
           s.path.text, 0x34465b, font, false);
  addHot(pathX, pathY, pathW, pathH, HotKind::EditPath);

  auto field = [&](int& fx, int fy, int labelW, int boxW, const char* label, const Edit& edit, Focus which,
                   const char* hint, bool invalid, HotKind kind) {
    drawText(frame, fx, fy, labelW, row2, label, 0x34495e, font, false);
    fx += labelW;
    uint32_t border = invalid ? 0xc0392b : s.focus == which ? 0x6b9dcd : 0xdce3ec;
    fillRect(frame, fx, fy, boxW, row2, 0xffffff);
    strokeRect(frame, fx, fy, boxW, row2, border, s.focus == which || invalid ? 2 : 1);
    const std::string& shownText = edit.text.empty() && s.focus != which ? std::string(hint) : edit.text;
    uint32_t color = edit.text.empty() && s.focus != which ? 0x95a5a6 : 0x1f2933;
    drawText(frame, fx + 4, fy, boxW - 8, row2, shownText, color, font, false);
    addHot(fx, fy, boxW, row2, kind);
    fx += boxW + gap;
  };

  x = gap;
  y = gap + row1 + gap + pathRow;
  int filterLabelW = scaled(input.dpi, 36);
  int minBoxW = std::min(scaled(input.dpi, 90), std::max(40, width / 7));
  int boxW = std::max(20, (width - gap * 5 - filterLabelW * 3 - minBoxW) / 2);
  field(x, y, filterLabelW, boxW, zh::include(), s.inc, Focus::Inc, zh::hintInclude(), false, HotKind::EditInc);
  field(x, y, filterLabelW, boxW, zh::exclude(), s.exc, Focus::Exc, zh::hintExclude(), false, HotKind::EditExc);
  field(x, y, filterLabelW, minBoxW, zh::minSize(), s.min, Focus::Min, zh::hintMin(),
        s.minInvalid, HotKind::EditMin);

  int mapX = gap;
  int mapY = top;
  int mapW = std::max(1, width - gap * 2);
  int mapH = std::max(1, height - statusH - top - gap);
  fillRect(frame, mapX, mapY, mapW, mapH, categoryColor(CatDir));
  if (hasTree) {
    // The view root is not a child tile. Reserve its title independently so
    // entering a file-only directory cannot remove the outer container header.
    const int rootTitleH = std::min(titlePx, mapH);
    const uint32_t rootBase = categoryColor(CatDir);
    fillRect(frame, mapX, mapY, mapW, rootTitleH, shadeColor(rootBase, 12));
    fillRect(frame, mapX, mapY + rootTitleH - 1, mapW, 1, shadeColor(rootBase, -30));
    const uint64_t rootSize = s.view < s.visible.size() ? s.visible[s.view] : s.shown->nodes[s.view].size;
    std::string rootLabel = std::string(nodeName(*s.shown, s.view)) + "  " + formatSize(rootSize);
    drawText(frame, mapX + 4, mapY, mapW - 8, rootTitleH - 1, rootLabel,
             0x1b2631, std::max(9, font - 2), false);
    addHot(mapX, mapY, mapW, rootTitleH, HotKind::Tile, s.view);
    RectF area{static_cast<double>(mapX), static_cast<double>(mapY + rootTitleH),
               static_cast<double>(mapW), static_cast<double>(std::max(0, mapH - rootTitleH))};
    const int labelFont = std::max(9, scaled(input.dpi, 10));
    const int compactFont = std::max(8, scaled(input.dpi, 8));
    const double minW = compactFont * 2 + 4;
    const double minH = compactFont + 4;
    std::vector<uint64_t> layoutVisible = s.visible;
    if (!s.options.showUnknown && s.shown->pending < layoutVisible.size()) {
      uint64_t unknown = layoutVisible[s.shown->pending];
      layoutVisible[s.shown->pending] = 0;
      if (!layoutVisible.empty() && layoutVisible[0] >= unknown) layoutVisible[0] -= unknown;
    }
    RectF contentArea = area;
    double freeWidth = 0;
    if (s.options.showFree && s.view == 0 && s.shown->freeBytes) {
      double used = layoutVisible.empty() ? 0 : static_cast<double>(layoutVisible[0]);
      freeWidth = area.w * (static_cast<double>(s.shown->freeBytes) / (used + s.shown->freeBytes));
      if (freeWidth < minW) freeWidth = 0;
      contentArea.w -= freeWidth;
    }
    std::vector<Tile> laid = layoutTreemap(*s.shown, layoutVisible, s.view, contentArea,
                                           titlePx, minW, minH, s.expanded, s.options.detail);
    if (freeWidth >= minW) {
      Tile free; free.flags = kFree; free.category = CatFree; free.size = s.shown->freeBytes;
      free.outer = {area.x + contentArea.w, area.y, freeWidth, area.h}; laid.push_back(free);
    }
    s.motion.setTarget(laid, input.timeSec, s.view, area, s.snapLayout);
    s.snapLayout = false;
    s.tiles = s.motion.sample(input.timeSec);
    s.frameAnim = s.motion.running(input.timeSec);
    if (s.shown->changeGen != s.seenChange) {
      s.seenChange = s.shown->changeGen;
      for (uint32_t id : s.shown->changed) s.changes[id] = input.timeSec;
    }
    for (auto it = s.changes.begin(); it != s.changes.end();) {
      if (input.timeSec - it->second >= 0.4) it = s.changes.erase(it);
      else ++it;
    }
    s.frameAnim = s.frameAnim || !s.changes.empty();
    int dividerPx = 1;
    // Navigation layers overlap. Paint fills and borders together so an
    // outgoing border cannot show through the incoming directory.
    const bool layered = std::any_of(s.tiles.begin(), s.tiles.end(),
                                     [](const Tile& tile) { return tile.exiting; });
    const bool gpu = !layered && openclFillTiles(frame, s.tiles, mapX, mapY, mapW, mapH, dividerPx, s.options.fileGroups);
    int hoverTile = -1;
    for (int i = 0; i < static_cast<int>(s.tiles.size()); ++i) {
      const Tile& tile = s.tiles[static_cast<size_t>(i)];
      int tx = iFloor(tile.outer.x);
      int ty = iFloor(tile.outer.y);
      int tw = iFloor(tile.outer.x + tile.outer.w) - tx;
      int th = iFloor(tile.outer.y + tile.outer.h) - ty;
      if (tile.hidden || tw <= 0 || th <= 0) continue;
      if (!tile.exiting && input.mouseX >= tx && input.mouseY >= ty && input.mouseX < tx + tw && input.mouseY < ty + th) hoverTile = i;
      uint32_t base = categoryColor(tile.flags & kDir ? CatDir : s.options.fileGroups ? tile.category : CatOther);
      if (tile.flags & kFree) base = categoryColor(CatFree);
      if (tile.flags & kPending) base = 0xf5cba7;
      uint32_t topColor = shadeColor(base, 5);
      uint32_t bottomColor = shadeColor(base, -5);
      uint32_t titleColor = shadeColor(base, 12);
      uint32_t dividerColor = shadeColor(base, -30);
      if (!gpu) {
        if (th < 8) fillRect(frame, tx, ty, tw, th, base);
        else fillGradient(frame, tx, ty, tw, th, topColor, bottomColor);
      }
      strokeRect(frame, tx, ty, tw, th, shadeColor(base, -30), 1);
      uint32_t labelColor = mixColor(base, 0x1b2631,
                                    static_cast<int>(tile.labelOpacity * 255), 255);
      std::string name = tileLabel(*s.shown, tile);
      std::string sizeText = formatSize(tile.size);
      bool labelVisible = tile.labelOpacity > 0.05;
      if (tile.exiting && labelVisible) {
        // Text is composited after the rectangles by each native host. Do not
        // leave an old label floating above a tile that has covered it.
        double cx = tile.outer.x + tile.outer.w * 0.5;
        double cy = tile.container ? tile.title.y + tile.title.h * 0.5
                                   : tile.outer.y + tile.outer.h * 0.5;
        for (size_t j = static_cast<size_t>(i) + 1; j < s.tiles.size(); ++j) {
          const RectF& cover = s.tiles[j].outer;
          if (cx >= cover.x && cy >= cover.y && cx < cover.x + cover.w && cy < cover.y + cover.h) {
            labelVisible = false;
            break;
          }
        }
      }
      const bool compact = tw < labelFont * 10 || th < labelFont * 4;
      const int tileFont = compact ? compactFont : labelFont;
      if (labelVisible && tile.title.h >= compactFont + 1) {
        int hy = iFloor(tile.title.y);
        int hh = iFloor(tile.title.y + tile.title.h) - hy;
        int dh = 1;
        int textH = hh - dh;
        if (!gpu) {
          fillRect(frame, tx, hy, tw, hh, titleColor);
          fillRect(frame, tx, hy + hh - dh, tw, dh, dividerColor);
        }
        int headerFont = tw < labelFont * 12 ? compactFont : labelFont;
        if (textH >= headerFont && tw >= headerFont * 2 + 4) {
          std::string header = name;
          if (tw >= headerFont * 14) header += "  " + sizeText;
          if (tw < headerFont * 10) header = compactTileText(header, tw - 4, headerFont);
          drawText(frame, tx + 2, hy, tw - 4, textH, header, labelColor, headerFont, false);
        }
      } else if (labelVisible && !tile.container && tw >= tileFont * 2 + 2 && th >= tileFont + 2) {
        std::string shortName = compact ? compactTileText(name, tw - 4, tileFont) : name;
        if (th >= tileFont * 2 + 4) {
          std::string compactSize = sizeText;
          if (compact) compactSize.erase(std::remove(compactSize.begin(), compactSize.end(), ' '), compactSize.end());
          int line = tileFont + 1;
          int y0 = ty + (th - line * 2) / 2;
          drawText(frame, tx + 2, y0, tw - 4, line, shortName, labelColor, tileFont, true);
          if (tw >= tileFont * 4)
            drawText(frame, tx + 2, y0 + line, tw - 4, line, compactSize, labelColor, tileFont, true);
        } else {
          drawText(frame, tx + 2, ty + 1, tw - 4, th - 2, shortName, labelColor, tileFont, true);
        }
      }
      if (!tile.exiting) addHot(tx, ty, tw, th, HotKind::Tile, tile.mergeCount ? kNone : tile.index);
    }
    // Brighten the finished pixels so CPU/GPU fills and nested borders keep
    // the same appearance. Native text is drawn afterwards and stays crisp.
    auto brighten = [&](RectF rect, int amount) {
      int x0 = std::max(mapX, iFloor(rect.x));
      int y0 = std::max(mapY, iFloor(rect.y));
      int x1 = std::min(mapX + mapW, iFloor(rect.x + rect.w));
      int y1 = std::min(mapY + mapH, iFloor(rect.y + rect.h));
      for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) {
          auto& pixel = frame.bgra[static_cast<size_t>(yy) * frame.width + xx];
          pixel = packRgb(shadeColor(pixel, amount));
        }
      }
    };
    std::vector<int> brightness(s.tiles.size(), 0);
    for (const auto& change : s.changes) {
      uint32_t id = change.first;
      int match = -1;
      while (id != kNone && id < s.shown->nodes.size()) {
        for (size_t i = 0; i < s.tiles.size(); ++i) {
          if (!s.tiles[i].hidden && !s.tiles[i].exiting && s.tiles[i].index == id) { match = static_cast<int>(i); break; }
        }
        if (match >= 0 || id == s.view) break;
        // A hidden small file may live in an aggregate block.
        for (size_t i = 0; i < s.tiles.size(); ++i) {
          if (!s.tiles[i].hidden && !s.tiles[i].exiting && s.tiles[i].mergeCount &&
              s.tiles[i].parent == s.shown->nodes[id].parent) { match = static_cast<int>(i); break; }
        }
        if (match >= 0) break;
        id = s.shown->nodes[id].parent;
      }
      double age = input.timeSec - change.second;
      int amount = static_cast<int>(age < 0.12 ? 26 - 18 * age / 0.12 : 8 * (0.4 - age) / 0.28);
      if (match >= 0) brightness[match] = std::max(brightness[match], amount);
      else if (id == s.view) brighten({static_cast<double>(mapX), static_cast<double>(mapY),
                                      static_cast<double>(mapW), static_cast<double>(rootTitleH)}, amount);
    }
    uint64_t hovered = hoverTile < 0 ? static_cast<uint64_t>(kNone) :
        s.tiles[hoverTile].index != kNone ? s.tiles[hoverTile].index :
        (1ull << 63) | s.tiles[hoverTile].parent;
    if (s.hovered != hovered) { s.hovered = hovered; s.hoverSince = input.timeSec; }
    if (hoverTile >= 0) {
      double age = input.timeSec - s.hoverSince;
      brightness[hoverTile] = std::max(brightness[hoverTile],
                                     static_cast<int>(age < 0.12 ? 44 - 18 * age / 0.12 : 26));
      s.frameAnim = s.frameAnim || age < 0.12;
    }
    for (size_t i = 0; i < s.tiles.size(); ++i) {
      if (!s.tiles[i].hidden && !s.tiles[i].exiting && s.tiles[i].index == s.selected) brightness[i] = std::max(brightness[i], 10);
      if (!s.tiles[i].hidden && brightness[i] > 0) brighten(s.tiles[i].outer, brightness[i]);
    }
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
    fillRect(frame, mapX, mapY, mapW, mapH, 0xf0f0f0);
    if (s.shown && !s.shown->error.empty())
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
      status += s.archived ? "\u5feb\u7167 (\u79bb\u7ebf)" : zh::scanDone();
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
  {
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
