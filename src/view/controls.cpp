#include "app/state.hpp"
#include "view/icons.hpp"
#include "view/metrics.hpp"
#include "view/progress.hpp"
#include "view/raster.hpp"
#include "i18n/text.hpp"

#include <algorithm>
#include <cmath>

int App::State::renderControls(const Input& input, const Host& host, Frame& frame, std::vector<HotRect>& next) {
  State& s = *this;
  const ViewMetrics v(input);
  const int width = v.width, height = v.height, gap = v.gap, row1 = v.row1, row2 = v.row2;
  const int statusH = v.statusH, top = v.top, controlFont = v.controlFont;
  fillRect(frame, 0, 0, width, top, 0xf8fafc);
  fillRect(frame, 0, gap + row1 + gap / 2, width, 1, 0xe5eaf0);
  fillRect(frame, 0, height - statusH, width, statusH, 0xe7ebf0);
  fillRect(frame, 0, top, width, 1, 0xd0d7de);
  fillRect(frame, 0, height - statusH, width, 1, 0xd0d7de);
  const double toolScale = std::min(1.0, static_cast<double>(std::max(1, width - gap * 16)) /
                                            scaled(input.controlDpi(), 912));
  auto button = [&](int& x, int y, int w, int h, const char* label, HotKind kind, bool enabled) {
    w = std::max(1, static_cast<int>(w * toolScale));
    bool hover = enabled && input.mouseX >= x && input.mouseY >= y && input.mouseX < x + w && input.mouseY < y + h;
    const bool primary = kind == HotKind::Open;
    const bool checked = (kind == HotKind::FileGroups && s.options.fileGroups) ||
                         (kind == HotKind::ShowFree && s.options.showFree) ||
                         (kind == HotKind::ShowUnknown && s.options.showUnknown);
    uint32_t bg = hover ? 0xe5effa : (primary || checked) ? 0xeaf2fc : 0xf8fafc;
    roundRect(frame, x, y, w, h, scaled(input.controlDpi(), 7), bg);
    uint32_t ink = !enabled ? 0xb6c0cc : kind == HotKind::Stop ? 0xc36b63 : 0x35658e;
    int iconSize = std::max(12, std::min(scaled(input.controlDpi(), 24), w - 8));
    toolbarIcon(frame, x + (w - iconSize) / 2, y + scaled(input.controlDpi(), 7), iconSize, kind,
                ink, enabled ? 0xd6e6f7 : 0xe9edf2);
    drawText(frame, x + 2, y + scaled(input.controlDpi(), 36), w - 4, scaled(input.controlDpi(), 24),
             label, enabled ? 0x34465b : 0xaab5c1, std::min(controlFont, std::max(8, w / 4)), true);
    if (enabled) addHot(next, x, y, w, h, kind);
    x += w + gap;
  };
  int x = gap, y = gap;
  const bool running = s.scanner.running(), hasTree = s.shown && !s.shown->nodes.empty();
  button(x, y, scaled(input.controlDpi(), 64), row1, zh::open(), HotKind::Open, true);
  const int driveX = x;
  button(x, y, scaled(input.controlDpi(), 64), row1, zh::back(), HotKind::Back, !s.back.empty());
  button(x, y, scaled(input.controlDpi(), 64), row1, zh::forward(), HotKind::Forward, !s.forward.empty());
  button(x, y, scaled(input.controlDpi(), 64), row1, zh::up(), HotKind::Up, hasTree && s.view != 0);
  button(x, y, scaled(input.controlDpi(), 56), row1, zh::rootButton(), HotKind::Home, hasTree && s.view != 0);
  button(x, y, scaled(input.controlDpi(), 80), row1, zh::rescan(), HotKind::Rescan, !s.path.text.empty());
  button(x, y, scaled(input.controlDpi(), 64), row1, s.scanner.stopRequested() ? zh::stopping() : zh::stop(),
         HotKind::Stop, running);
  button(x, y, scaled(input.controlDpi(), 64), row1, zh::exportCsv(), HotKind::Export, hasTree && !s.exportRunning);
  button(x, y, scaled(input.controlDpi(), 56), row1, zh::focusRescan(), HotKind::FocusRescan, hasTree && !running);
  button(x, y, scaled(input.controlDpi(), 56), row1, zh::lessDetailButton(), HotKind::LessDetail, hasTree && s.options.detail > 0);
  button(x, y, scaled(input.controlDpi(), 56), row1, zh::moreDetailButton(), HotKind::MoreDetail, hasTree && s.options.detail < 6);
  button(x, y, scaled(input.controlDpi(), 56), row1, zh::fileTypeButton(), HotKind::FileGroups, hasTree);
  button(x, y, scaled(input.controlDpi(), 56), row1, zh::showFreeButton(), HotKind::ShowFree, hasTree);
  button(x, y, scaled(input.controlDpi(), 56), row1, zh::showUnknownButton(), HotKind::ShowUnknown, hasTree);
  button(x, y, scaled(input.controlDpi(), 56), row1, zh::preferencesButton(), HotKind::Configure, static_cast<bool>(host.configure));
  const int pathX = gap, pathY = gap + row1 + gap, pathH = scaled(input.controlDpi(), 32);
  const int pathW = std::max(1, width - gap - pathX);
  uint32_t pathBorder = s.focus == Focus::Path ? 0x6b9dcd : 0xdce3ec;
  roundRect(frame, pathX, pathY, pathW, pathH, scaled(input.controlDpi(), 5), pathBorder);
  roundRect(frame, pathX + 1, pathY + 1, pathW - 2, pathH - 2, scaled(input.controlDpi(), 4), 0xffffff);
  drawText(frame, pathX + scaled(input.controlDpi(), 10), pathY, pathW - scaled(input.controlDpi(), 20), pathH,
           s.path.text, 0x34465b, controlFont, false);
  addHot(next, pathX, pathY, pathW, pathH, HotKind::EditPath);
  auto field = [&](int& fx, int fy, int labelW, int boxW, const char* label, const Edit& edit, Focus which,
                   const char* hint, bool invalid, HotKind kind) {
    drawText(frame, fx, fy, labelW, row2, label, 0x34495e, controlFont, false);
    fx += labelW;
    uint32_t border = invalid ? 0xc0392b : s.focus == which ? 0x6b9dcd : 0xdce3ec;
    fillRect(frame, fx, fy, boxW, row2, 0xffffff);
    strokeRect(frame, fx, fy, boxW, row2, border, s.focus == which || invalid ? 2 : 1);
    const std::string& shownText = edit.text.empty() && s.focus != which ? std::string(hint) : edit.text;
    uint32_t color = edit.text.empty() && s.focus != which ? 0x95a5a6 : 0x1f2933;
    drawText(frame, fx + 4, fy, boxW - 8, row2, shownText, color, controlFont, false);
    addHot(next, fx, fy, boxW, row2, kind);
    fx += boxW + gap;
  };
  x = gap;
  y = gap + row1 + gap + v.pathRow;
  const int filterLabelW = scaled(input.controlDpi(), 36);
  const int minBoxW = std::min(scaled(input.controlDpi(), 90), std::max(40, width / 7));
  const int boxW = std::max(20, (width - gap * 5 - filterLabelW * 3 - minBoxW) / 2);
  field(x, y, filterLabelW, boxW, zh::include(), s.inc, Focus::Inc, zh::hintInclude(), false, HotKind::EditInc);
  field(x, y, filterLabelW, boxW, zh::exclude(), s.exc, Focus::Exc, zh::hintExclude(), false, HotKind::EditExc);
  field(x, y, filterLabelW, minBoxW, zh::minSize(), s.min, Focus::Min, zh::hintMin(), s.minInvalid, HotKind::EditMin);

  const int progressX = gap, progressW = std::max(1, width - gap * 2);
  fillRect(frame, progressX, top, progressW, v.progressH, 0xe3e8ee);
  if (running) {
#if defined(_WIN32)
    const ScanProgress progress = scanProgress(s.shown.get());
    if (progress.determinate) {
      const int filled = static_cast<int>(std::lround(progressW * std::min(progress.fraction, 0.99)));
      fillRect(frame, progressX, top, filled, v.progressH, 0x4388c7);
    } else {
#endif
      const int segmentW = std::max(progressW / 5, scaled(input.dpi, 72));
      const int travel = progressW + segmentW;
      const int offset = static_cast<int>(std::fmod(s.shown ? s.shown->elapsed * progressW * 0.9 : 0, travel)) - segmentW;
      const int left = std::max(0, offset), right = std::min(progressW, offset + segmentW);
      fillRect(frame, progressX + left, top, right - left, v.progressH, 0x4388c7);
#if defined(_WIN32)
    }
#endif
  } else if (s.shown && s.shown->complete && !s.shown->aborted) {
    fillRect(frame, progressX, top, progressW, v.progressH, 0x4388c7);
  }
  return driveX;
}
