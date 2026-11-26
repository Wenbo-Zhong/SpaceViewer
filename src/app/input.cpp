#include "app/state.hpp"
#include "i18n/text.hpp"

#include <cmath>

void App::State::handleInput(const Input& input, const Host& host) {
  State& s = *this;
  if (input.paste && host.getClipboard) {
    if (Edit* edit = s.focusedEdit()) insertText(*edit, host.getClipboard());
  }
  if (Edit* edit = s.focusedEdit()) {
    if (input.copy && host.setClipboard) {
      host.setClipboard(edit->text);
      s.setNote(zh::copied(), input.timeSec);
    }
    editKey(*edit, input);
    if (input.keyTab) s.focus = static_cast<Focus>((static_cast<int>(s.focus) % 4) + 1);
    if (input.keyEsc) s.focus = Focus::None;
    if (input.keyEnter && s.focus == Focus::Path) s.scan(s.path.text);
    if (input.keyEnter && s.focus == Focus::Min) s.focus = Focus::None;
  } else {
    if (input.keyBackspace || input.keyUpNav) {
      s.execute(HotKind::Up, host, input.timeSec);
    } else if (input.keyBackNav && !s.back.empty()) {
      s.execute(HotKind::Back, host, input.timeSec);
    } else if (input.keyForwardNav && !s.forward.empty()) {
      s.execute(HotKind::Forward, host, input.timeSec);
    } else if (input.keyEsc) {
      if (s.driveOpen) s.driveOpen = false;
      else s.execute(s.scanner.running() ? HotKind::Stop : HotKind::Up, host, input.timeSec);
    } else if (input.keyEnter && s.selected != kNone && s.shown && s.selected < s.shown->nodes.size()) {
      const Node& node = s.shown->nodes[s.selected];
      if ((node.flags & kDir) && !(node.flags & (kFree | kPending))) s.navigate(s.selected);
      else if (host.openPath) {
        std::string path = s.pathOf(s.selected);
        if (!path.empty()) host.openPath(path);
      }
    } else if (input.copy && s.selected != kNone) s.copyIndex(s.selected, host, input.timeSec);
  }
  if (input.keyF5) s.execute(HotKind::Rescan, host, input.timeSec);
  if (input.openShortcut && host.pickFolder) s.execute(HotKind::Open, host, input.timeSec);

  if (input.leftPressed || input.rightPressed || input.command >= 0) {
    int index = hitHot(s.hots, input.mouseX, input.mouseY);
    HotRect commandHot;
    if (input.command >= 0) commandHot.kind = static_cast<HotKind>(input.command);
    const HotRect* hot = input.command >= 0 ? &commandHot
        : index >= 0 ? &s.hots[static_cast<size_t>(index)] : nullptr;
    bool onPopup = hot && (hot->kind == HotKind::DriveItem || hot->kind == HotKind::Drives);
    if (!onPopup) s.driveOpen = false;
    if (input.leftPressed || input.command >= 0) {
      double previousClick = s.lastClick;
      int previousX = s.lastX, previousY = s.lastY;
      uint32_t previousSelected = s.selected;
      bool second = input.command < 0 && s.lastZoom && (input.timeSec - s.lastClick) < 0.35 &&
                    std::abs(input.mouseX - s.lastX) < 6 && std::abs(input.mouseY - s.lastY) < 6;
      s.lastClick = input.timeSec;
      s.lastX = input.mouseX;
      s.lastY = input.mouseY;
      if (second) {
        // 首击展开后鼠标下可能出现子节点，双击仍应作用于首击选中的目录。
        if (s.shown && previousSelected < s.shown->nodes.size() &&
            (s.shown->nodes[previousSelected].flags & kDir)) s.navigate(previousSelected);
        s.lastZoom = false;
      } else if (!hot) {
        s.focus = Focus::None;
        s.selected = kNone;
        s.lastZoom = false;
      } else {
        switch (hot->kind) {
          case HotKind::EditPath:
          case HotKind::EditInc:
          case HotKind::EditExc:
          case HotKind::EditMin:
            s.focus = hot->kind == HotKind::EditPath ? Focus::Path
                : hot->kind == HotKind::EditInc ? Focus::Inc
                : hot->kind == HotKind::EditExc ? Focus::Exc : Focus::Min;
            if (Edit* edit = s.focusedEdit()) {
              edit->cursor = edit->text.size();
              edit->selectAll = false;
            }
            break;
          case HotKind::DriveItem:
            if (hot->driveIndex >= 0 && hot->driveIndex < static_cast<int>(s.drives.size()))
              s.scan(s.drives[static_cast<size_t>(hot->driveIndex)].path);
            break;
          case HotKind::Tile:
            s.clickTile(*hot, input, host, previousClick, previousX, previousY, previousSelected);
            break;
          default: s.execute(hot->kind, host, input.timeSec); break;
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
      s.menu.canOpen = s.menu.canReveal = s.menu.canCopy = !synthetic;
      s.menu.canZoom = dir;
      s.menuPending = true;
      s.menuNode = hot->index;
    }
  }
}
