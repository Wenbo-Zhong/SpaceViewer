#include "app/state.hpp"
#include "io/export.hpp"
#include "i18n/text.hpp"

#include <algorithm>

// 键盘、工具栏和原生命令共用此入口，新增通用动作时保持各入口行为一致。
void App::State::execute(HotKind kind, const Host& host, double now) {
  switch (kind) {
    case HotKind::Open:
      if (host.newWindow && shown && !shown->nodes.empty()) { host.newWindow(); break; }
      if (host.pickFolder) {
        std::string picked;
        if (host.pickFolder(picked)) scan(picked);
      }
      break;
    case HotKind::Drives:
      if (driveOpen) driveOpen = false;
      else if (host.volumes) { drives = host.volumes(); driveOpen = true; }
      break;
    case HotKind::Back:
      if (!back.empty()) {
        forward.push_back(view);
        view = back.back();
        back.pop_back();
      }
      break;
    case HotKind::Forward:
      if (!forward.empty()) {
        back.push_back(view);
        view = forward.back();
        forward.pop_back();
      }
      break;
    case HotKind::Up:
      if (shown && view != 0 && view < shown->nodes.size()) navigate(shown->nodes[view].parent);
      break;
    case HotKind::Rescan: {
      std::string again = path.text;
      if (shown && view < shown->nodes.size()) {
        std::string current = nodePath(*shown, view);
        if (!current.empty()) again = current;
      }
      scan(again);
      break;
    }
    case HotKind::Stop: scanner.requestStop(); break;
    case HotKind::Export:
      if (host.pickSave && shown && !shown->nodes.empty() && !exportRunning) {
        std::string dest;
        if (host.pickSave(dest)) {
          if (exportThread.joinable()) exportThread.join();
          // 固定导出启动时的快照、过滤条件和根节点，任务期间导航或重扫不改变导出内容。
          auto snap = shown;
          Filter exportFilter = filter;
          uint32_t root = view;
          exportRunning = true;
          setNote(zh::exporting(), now);
          std::function<void()> callback = wake;
          exportThread = std::thread([this, snap, exportFilter, root, dest, callback]() {
            std::string error;
            bool ok = writeCsv(*snap, exportFilter, root, dest, error);
            {
              std::lock_guard<std::mutex> lock(noteMu);
              exportRunning = false;
              note = ok ? (std::string(zh::exportOk()) + " " + dest) : error;
              noteUntil = 1e12;
            }
            if (callback) callback();
          });
        }
      }
      break;
    case HotKind::Home: navigate(0); break;
    case HotKind::FocusRescan: {
      uint32_t id = selected == kNone ? view : selected;
      if (shown && id < shown->nodes.size()) {
        if (!(shown->nodes[id].flags & kDir)) id = shown->nodes[id].parent;
        if (archived) {
          std::string current = pathOf(id);
          if (!current.empty()) scan(current);
        } else scanner.refreshSubtree(id);
      }
      break;
    }
    case HotKind::LessDetail:
      options.detail = std::max(0, options.detail - 1);
      expanded.clear();
      snapLayout = true;
      break;
    case HotKind::MoreDetail:
      options.detail = std::min(6, options.detail + 1);
      snapLayout = true;
      break;
    case HotKind::FileGroups: options.fileGroups = !options.fileGroups; break;
    case HotKind::ShowFree: options.showFree = !options.showFree; snapLayout = true; break;
    case HotKind::ShowUnknown: options.showUnknown = !options.showUnknown; snapLayout = true; break;
    case HotKind::Configure: if (host.configure) host.configure(); break;
    case HotKind::CloseView:
      scanner.loadSnapshot(TreeSnapshot{});
      view = 0;
      path.text.clear();
      path.cursor = 0;
      selected = kNone;
      expanded.clear();
      break;
    default: break;  // Fields and tiles require hit-region data.
  }
}
