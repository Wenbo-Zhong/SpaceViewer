#include "app/state.hpp"

#include "io/export.hpp"
#include "i18n/text.hpp"
#include "i18n/format.hpp"

#include <algorithm>

App::State::~State() {
  if (exportThread.joinable()) exportThread.join();
}

void App::State::setNote(const std::string& text, double now) {
  std::lock_guard<std::mutex> lock(noteMu);
  note = text;
  noteUntil = now + 4.0;
}

std::string App::State::currentNote(double now) {
  std::lock_guard<std::mutex> lock(noteMu);
  if (now > noteUntil) return {};
  return note;
}

void App::State::scan(const std::string& pathText) {
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
  permissionWarningShown = false;
  if (globMatch(pathText, "*.svs")) {
    TreeSnapshot loaded;
    std::string error;
    if (readSnapshot(pathText, loaded, error)) {
      scanner.loadSnapshot(std::move(loaded));
      archived = true;
    } else {
      std::lock_guard<std::mutex> lock(noteMu);
      note = error;
      noteUntil = 1e12;
    }
  } else scanner.start(pathText);
}

Edit* App::State::focusedEdit() {
  if (focus == Focus::Path) return &path;
  if (focus == Focus::Inc) return &inc;
  if (focus == Focus::Exc) return &exc;
  if (focus == Focus::Min) return &min;
  return nullptr;
}

void App::State::refreshFilter() {
  filter.includeGlobs = splitList(inc.text);
  filter.excludeGlobs = splitList(exc.text);
  uint64_t parsed = 0;
  if (min.text.empty()) {
    filter.minSize = 0;
    minInvalid = false;
  } else if (parseSize(min.text, parsed)) {
    filter.minSize = parsed;
    minInvalid = false;
  } else minInvalid = true;
}

void App::State::navigate(uint32_t index) {
  if (index == view) return;
  forward.clear();
  back.push_back(view);
  if (back.size() > 64) back.erase(back.begin());
  view = index;
  selected = index;
}

std::string App::State::pathOf(uint32_t index) const {
  if (!shown || index >= shown->nodes.size()) return {};
  return nodePath(*shown, index);
}

void App::State::copyIndex(uint32_t index, const Host& host, double now) {
  std::string pathText = pathOf(index);
  if (pathText.empty() || !host.setClipboard) return;
  host.setClipboard(pathText);
  setNote(zh::copied(), now);
}

void App::State::refreshSnapshot(const Host& host) {
  refreshFilter();
  shown = scanner.snapshot();
  if (shown && shown->complete && shown->permissionErrors > 0 && !archived &&
      !permissionWarningShown && host.permissionWarning) {
    permissionWarningShown = true;
    host.permissionWarning(shown->permissionErrors);
  }
  const TreeSnapshot* key = shown.get();
  if (key != visibleKey || inc.text != visibleInc || exc.text != visibleExc ||
      filter.minSize != visibleMin) {
    visible = shown ? computeVisible(*shown, filter) : std::vector<uint64_t>{};
    visibleKey = key;
    visibleInc = inc.text;
    visibleExc = exc.text;
    visibleMin = filter.minSize;
  }
  if (!shown || shown->nodes.empty() || view >= shown->nodes.size()) view = 0;
  if (focus != Focus::Path && shown && !shown->nodes.empty()) {
    std::string current = nodePath(*shown, view);
    if (!current.empty() && current != path.text) {
      path.text = current;
      path.cursor = path.text.size();
    }
  }
}

void App::State::updateTitle(const Host& host) {
  std::string title = "SpaceViewer";
  if (!path.text.empty()) title += " - " + path.text;
  if (title != lastTitle && host.setTitle) {
    lastTitle = title;
    host.setTitle(title);
  }
}

App::App() : state(new State) {}
App::~App() { delete state; }

void App::setWake(std::function<void()> wake) {
  state->wake = wake;
  state->scanner.setWake(wake);
}

void App::scanPath(const std::string& path) { state->scan(path); }
void App::requestStop() { state->scanner.requestStop(); }
ViewOptions App::options() const { return state->options; }
void App::setOptions(const ViewOptions& options) {
  state->options = options;
  state->options.detail = std::clamp(options.detail, 0, 6);
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
  state->back.clear();
  state->forward.clear();
  state->expanded.clear();
  state->changes.clear();
  state->view = 0;
  state->selected = kNone;
  state->seenChange = 0;
  state->scanner.loadSnapshot(std::move(tree));
  state->archived = true;
  state->snapLayout = true;
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
  const uint32_t index = state->menuNode;
  if (!state->shown || index >= state->shown->nodes.size()) return;
  if (command == MenuCommand::Open && host.openPath) {
    std::string path = state->pathOf(index);
    if (!path.empty()) host.openPath(path);
  } else if (command == MenuCommand::Reveal && host.revealPath) {
    std::string path = state->pathOf(index);
    if (!path.empty()) host.revealPath(path);
  } else if (command == MenuCommand::CopyPath) {
    state->copyIndex(index, host, 0);
    state->noteUntil = 4;
  } else if (command == MenuCommand::Zoom) state->navigate(index);
}

void App::update(const Input& input, const Host& host, Frame& frame) {
  // Events refer to the last presented frame. Publish new hit regions only
  // after input, snapshot synchronization and drawing have finished.
  state->handleInput(input, host);
  state->refreshSnapshot(host);
  state->render(input, host, frame);
  state->updateTitle(host);
}
