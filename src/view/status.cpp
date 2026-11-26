#include "app/state.hpp"
#include "view/metrics.hpp"
#include "view/progress.hpp"
#include "view/raster.hpp"
#include "i18n/format.hpp"
#include "i18n/text.hpp"

#include <algorithm>
#include <cmath>

void App::State::renderStatus(const Input& input, Frame& frame, const std::vector<HotRect>& next) {
  State& s = *this;
  const ViewMetrics v(input);
  const bool running = s.scanner.running(), hasTree = s.shown && !s.shown->nodes.empty();
  std::string status;
  if (hasTree) {
    status = std::string(zh::files()) + " " + formatCount(s.shown->files) + "   " +
             zh::folders() + " " + formatCount(s.shown->dirs) + "   " + zh::used() + " ";
    uint64_t used = s.shown->nodes[0].size;
    if (s.shown->pending < s.shown->nodes.size()) {
      uint64_t pending = s.shown->nodes[s.shown->pending].size;
      if (used >= pending) used -= pending;
    }
    status += formatSize(s.view < s.visible.size() ? s.visible[s.view] : used);
    if (s.shown->freeBytes) status += std::string("   ") + zh::avail() + " " + formatSize(s.shown->freeBytes);
    if (s.shown->errors) status += std::string("   ") + zh::errors() + " " + formatCount(s.shown->errors);
    status += "   ";
    if (running) {
      status += zh::scanning();
#if defined(_WIN32)
      const ScanProgress progress = scanProgress(s.shown.get());
      if (progress.determinate) status += " " + std::to_string(static_cast<int>(std::lround(progress.fraction * 100))) + "%";
#endif
      if (s.shown->elapsed > 0.3 && s.shown->files > 0) {
        double rate = static_cast<double>(s.shown->files) / s.shown->elapsed;
        status += " " + formatCount(static_cast<uint64_t>(rate)) + " " + zh::perSec();
      }
    } else if (s.shown->aborted) status += zh::stopped();
    else if (s.shown->complete) {
      status += s.archived ? zh::snapshotOffline() : zh::scanDone();
      if (s.shown->watchGap) status += std::string("   ") + zh::watchGap();
    } else status += zh::ready();
  } else status = zh::ready();
  std::string extra = s.currentNote(input.timeSec);
  if (s.minInvalid) extra = zh::invalidMin();
  std::string detail;
  uint32_t detailIndex = s.selected;
  int hover = hitHot(next, input.mouseX, input.mouseY);
  if (hover >= 0 && next[static_cast<size_t>(hover)].kind == HotKind::Tile)
    detailIndex = next[static_cast<size_t>(hover)].index;
  if (s.shown && detailIndex != kNone && detailIndex < s.shown->nodes.size()) {
    const Node& node = s.shown->nodes[detailIndex];
    const uint64_t size = detailIndex < s.visible.size() ? s.visible[detailIndex] : node.size;
    const uint64_t whole = s.view < s.visible.size() ? s.visible[s.view] : 0;
    detail = std::string(nodeName(*s.shown, detailIndex)) + "   " + formatSize(size) +
             "   " + zh::ofView() + " " + formatPercent(size, whole);
    std::string pathText = nodePath(*s.shown, detailIndex);
    if (!pathText.empty()) detail += "   " + pathText;
    if (node.flags & kPending && s.shown->complete) detail += std::string("   ") + zh::uncountedNote();
  } else if (running && s.shown && !s.shown->tip.empty()) detail = std::string(zh::scanning()) + "  " + s.shown->tip;
  if (!extra.empty()) {
    if (!detail.empty()) detail += "   ";
    detail += extra;
  }
  drawText(frame, v.gap, v.height - v.statusH, v.width / 2 - v.gap, v.statusH, status, 0x1f2933, v.font, false);
  drawText(frame, v.width / 2, v.height - v.statusH, v.width / 2 - v.gap, v.statusH, detail, 0x1f2933, v.font, false);
}

void App::State::renderDrivePopup(const Input& input, Frame& frame, std::vector<HotRect>& next, int driveX) {
  if (!driveOpen) return;
  const ViewMetrics v(input);
  const int row = scaled(input.dpi, 28);
  const int popupW = std::min(v.width - v.gap * 2, scaled(input.dpi, 460));
  const int popupX = std::min(driveX, v.width - popupW - v.gap);
  const int popupY = v.gap + v.row1, popupH = row * static_cast<int>(drives.size());
  fillRect(frame, popupX, popupY, popupW, std::max(row, popupH), 0xffffff);
  strokeRect(frame, popupX, popupY, popupW, std::max(row, popupH), 0x95a5a6, 1);
  if (drives.empty()) drawText(frame, popupX + 8, popupY, popupW - 16, row, zh::emptyFolder(), 0x7f8c8d, v.font, false);
  for (int i = 0; i < static_cast<int>(drives.size()); ++i) {
    const int ry = popupY + i * row;
    const bool hover = input.mouseX >= popupX && input.mouseY >= ry && input.mouseX < popupX + popupW &&
                       input.mouseY < ry + row;
    if (hover) fillRect(frame, popupX + 1, ry, popupW - 2, row, 0xd6eaf8);
    const VolumeEntry& volume = drives[static_cast<size_t>(i)];
    std::string line = volume.label.empty() ? volume.path : volume.label;
    line += "    " + formatSize(volume.free) + " / " + formatSize(volume.total);
    drawText(frame, popupX + 8, ry, popupW - 16, row, line, 0x1f2933, v.font, false);
    addHot(next, popupX, ry, popupW, row, HotKind::DriveItem, kNone, i);
  }
}
