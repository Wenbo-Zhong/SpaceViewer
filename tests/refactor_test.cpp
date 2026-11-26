#include "app/app.hpp"
#include "app/edit.hpp"
#include "i18n/text.hpp"
#include "i18n/unicode.hpp"
#include "io/export.hpp"
#include "scan/scanner.hpp"
#if !defined(_WIN32)
#include "platform/posix/volume.hpp"
#endif

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace {
namespace fs = std::filesystem;

void expect(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

struct TempDir {
  fs::path path;
  TempDir() {
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path = fs::temp_directory_path() / ("spaceviewer-refactor-" + std::to_string(stamp));
    expect(fs::create_directory(path), "create isolated temporary directory");
  }
  ~TempDir() {
    std::error_code error;
    fs::remove_all(path, error);
  }
  std::string file(const std::string& name) const { return (path / fs::u8path(name)).u8string(); }
  void write(const std::string& name, size_t size) const {
    std::ofstream out(fs::u8path(file(name)), std::ios::binary);
    out << std::string(size, 'x');
    expect(out.good(), "write fixture file");
  }
};

template <typename Predicate>
bool until(Predicate predicate) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  do {
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  } while (std::chrono::steady_clock::now() < deadline);
  return predicate();
}

std::shared_ptr<const TreeSnapshot> finished(Scanner& scanner) {
  expect(until([&]() { return scanner.snapshot()->complete; }), "scan finishes before deadline");
  auto tree = scanner.snapshot();
  expect(tree->error.empty() && !tree->aborted && !tree->running, "scan completes successfully");
  return tree;
}

uint32_t childNamed(const TreeSnapshot& tree, uint32_t parent, const std::string& name) {
  for (uint32_t id = tree.nodes[parent].firstChild; id != kNone; id = tree.nodes[id].nextSibling)
    if (nodeName(tree, id) == name) return id;
  return kNone;
}

void inputEdges() {
  Input input;
  input.windowW = 1400; input.windowH = 900; input.dpi = 192;
  input.mouseX = 451; input.mouseY = 612; input.timeSec = 42;
  input.modCtrl = input.modAlt = input.modShift = true;
  input.command = static_cast<int>(HotKind::MoreDetail);
  input.leftPressed = input.rightPressed = true;
  input.keyBackspace = input.keyDelete = input.keyEnter = input.keyTab = input.keyEsc = true;
  input.keyLeft = input.keyRight = input.keyHome = input.keyEnd = input.keyF5 = true;
  input.keyUpNav = input.keyBackNav = input.keyForwardNav = true;
  input.copy = input.paste = input.selectAll = input.openShortcut = true;
  input.typed = u8"中文😀";
  input.clearEdges();
  expect(input.command == -1 && !input.leftPressed && !input.rightPressed &&
         !input.keyBackspace && !input.keyDelete && !input.keyEnter && !input.keyTab && !input.keyEsc &&
         !input.keyLeft && !input.keyRight && !input.keyHome && !input.keyEnd && !input.keyF5 &&
         !input.keyUpNav && !input.keyBackNav && !input.keyForwardNav &&
         !input.copy && !input.paste && !input.selectAll && !input.openShortcut && input.typed.empty(),
         "all one-frame events are consumed");
  expect(input.windowW == 1400 && input.windowH == 900 && input.dpi == 192 &&
         input.mouseX == 451 && input.mouseY == 612 && input.timeSec == 42 &&
         input.modCtrl && input.modAlt && input.modShift, "persistent input state survives");
}

void unicodeEditing() {
  const std::string text = u8"A中😀B";
  const size_t boundaries[] = {0, 1, 4, 8, 9};
  for (size_t i = 1; i < 5; ++i) {
    expect(utf8Next(text, boundaries[i - 1]) == boundaries[i], "UTF8 forward character boundary");
    expect(utf8Prev(text, boundaries[i]) == boundaries[i - 1], "UTF8 backward character boundary");
  }
  expect(utf8Next(text, text.size()) == text.size() && utf8Prev(text, 0) == 0, "UTF8 endpoint clamping");
  expect(utf8Next(std::string("\xf0\x9f", 2), 0) == 2, "truncated UTF8 remains inside string");
  Edit edit;
  edit.text = text; edit.cursor = 8;
  Input key; key.keyBackspace = true;
  editKey(edit, key);
  expect(edit.text == u8"A中B" && edit.cursor == 4, "backspace removes entire supplementary character");
  key.clearEdges(); key.keyDelete = true;
  editKey(edit, key);
  expect(edit.text == u8"A中" && edit.cursor == 4, "delete preserves adjacent CJK");
}

void cjkRanges() {
  const uint32_t ranges[][2] = {
    {0x3400, 0x4dbf}, {0x4e00, 0x9fff}, {0xf900, 0xfaff}, {0x20000, 0x2fa1f},
    {0x3000, 0x303f}, {0xfe30, 0xfe4f}, {0xff00, 0xffef}
  };
  for (const auto& range : ranges) {
    expect(isChinese(range[0]) && isChinese(range[1]), "CJK range endpoints use bundled CJK font");
    expect(!isChinese(range[0] - 1) && !isChinese(range[1] + 1), "outside CJK ranges retains fallback font");
  }
  expect(!isChinese('A') && !isChinese(0x1f600) && lowerAscii('Z') == 'z' &&
         lowerAscii('0') == '0', "Latin and emoji classifications remain separate");
}

struct AppFixture {
  TempDir temp;
  App app;
  Host host;
  Frame frame;
  Input input;
  std::string title;
  AppFixture() {
    TreeSnapshot tree;
    tree.sep = '/'; tree.rootPath = "/fixture";
    addNode(tree, kNone, "fixture", kDir, CatDir, 0);
    uint32_t folder = addNode(tree, 0, "folder", kDir, CatDir, 0);
    addNode(tree, folder, "inside.bin", 0, CatOther, 100);
    addNode(tree, 0, "outside.bin", 0, CatOther, 100);
    accountBytes(tree, folder, 100); accountBytes(tree, 0, 100);
    std::string error;
    expect(writeSnapshot(tree, temp.file("tree.svs"), error), "write archived App fixture");
    expect(app.loadSnapshot(temp.file("tree.svs"), error), "load archived App fixture");
    host.setTitle = [&](const std::string& value) { title = value; };
    input.windowW = 1400; input.windowH = 900; input.dpi = 96;
    input.mouseX = -1; input.mouseY = -1;
    tick();
  }
  void tick() {
    input.timeSec += 1;
    app.update(input, host, frame);
    input.clearEdges();
  }
  void command(HotKind kind) { input.command = static_cast<int>(kind); tick(); }
  void toolbar(const char* label) {
    for (const auto& text : frame.texts) {
      if (text.utf8 != label) continue;
      input.mouseX = text.x + text.w / 2;
      input.mouseY = text.y + text.h / 2;
      input.leftPressed = true;
      tick();
      input.mouseX = input.mouseY = -1;
      return;
    }
    throw std::runtime_error("toolbar label is present");
  }
  void enterFolder() {
    for (const auto& text : frame.texts) {
      if (text.utf8 != "folder") continue;
      input.mouseX = text.x + text.w / 2;
      input.mouseY = text.y + text.h / 2;
      input.rightPressed = true;
      tick();
      MenuRequest menu;
      expect(app.takeMenu(menu) && menu.canZoom, "folder context menu can enter directory");
      app.applyMenu(MenuCommand::Zoom, host);
      input.mouseX = input.mouseY = -1;
      tick();
      expect(title == "SpaceViewer - /fixture/folder", "context menu enters selected directory");
      return;
    }
    throw std::runtime_error("folder tile is visible");
  }
};

void openCommands() {
  for (int entry = 0; entry < 3; ++entry) {
    AppFixture f;
    int windows = 0, picks = 0;
    f.host.newWindow = [&]() { ++windows; };
    f.host.pickFolder = [&](std::string&) { ++picks; return false; };
    if (entry == 0) { f.input.openShortcut = true; f.tick(); }
    if (entry == 1) f.command(HotKind::Open);
    if (entry == 2) f.toolbar(zh::open());
    expect(windows == 1 && picks == 0, "Open creates one new window when a view already exists");
    expect(f.title == "SpaceViewer - /fixture", "Open keeps current view");
  }
  AppFixture f;
  f.command(HotKind::CloseView);
  int picks = 0;
  f.host.pickFolder = [&](std::string&) { ++picks; return false; };
  f.input.openShortcut = true; f.tick();
  expect(picks == 1 && f.title == "SpaceViewer", "Open after closing a view invokes path picker");
}

void navigationCommands() {
  for (int entry = 0; entry < 3; ++entry) {
    AppFixture f; f.enterFolder();
    if (entry == 0) { f.input.keyUpNav = true; f.tick(); }
    if (entry == 1) f.command(HotKind::Up);
    if (entry == 2) f.toolbar(zh::up());
    expect(f.title == "SpaceViewer - /fixture", "Up returns to parent through every entry");
    if (entry == 0) { f.input.keyBackNav = true; f.tick(); }
    if (entry == 1) f.command(HotKind::Back);
    if (entry == 2) f.toolbar(zh::back());
    expect(f.title == "SpaceViewer - /fixture/folder", "Back restores previous directory");
    if (entry == 0) { f.input.keyForwardNav = true; f.tick(); }
    if (entry == 1) f.command(HotKind::Forward);
    if (entry == 2) f.toolbar(zh::forward());
    expect(f.title == "SpaceViewer - /fixture", "Forward restores parent directory");
  }
}

void detailCommands() {
  AppFixture f;
  for (int i = 0; i < 10; ++i) f.command(HotKind::MoreDetail);
  expect(f.app.options().detail == 6, "detail has an upper bound");
  for (int i = 0; i < 10; ++i) f.command(HotKind::LessDetail);
  expect(f.app.options().detail == 0, "detail has a lower bound");
  bool groups = f.app.options().fileGroups;
  f.command(HotKind::FileGroups);
  expect(f.app.options().fileGroups != groups, "file group command toggles the public option");
}

void scanBatches() {
  TempDir temp;
  fs::create_directories(temp.path / "sub" / "nested");
  for (int i = 0; i < 2105; ++i) temp.write("item" + std::to_string(i), 1 + i % 5);
  temp.write("sub/a.bin", 20); temp.write(u8"sub/nested/中文😀.bin", 37);
#if !defined(_WIN32)
  fs::create_directory_symlink(temp.path, temp.path / "loop");
#endif
  Scanner scanner; scanner.start(temp.path.u8string());
  auto tree = finished(scanner);
  expect(tree->nodes[0].size == 6372 && tree->files == 2107 && tree->dirs == 2,
         "parallel batches count every file exactly once and skip symbolic links");
  uint32_t sub = childNamed(*tree, 0, "sub");
  expect(sub != kNone && tree->nodes[sub].size == 57, "parallel subtree totals are complete");
  temp.write("sub/new.bin", 11);
  scanner.refreshSubtree(sub);
  tree = finished(scanner);
  expect(tree->nodes[0].size == 6383 && tree->nodes[sub].size == 68 &&
         tree->files == 2108 && tree->dirs == 2, "subtree refresh replaces counts without double charging");
}

void scanWatch() {
  TempDir temp;
  fs::create_directory(temp.path / "sub");
  temp.write("a.bin", 5); temp.write("sub/b.bin", 7);
  Scanner scanner; scanner.start(temp.path.u8string());
  finished(scanner);
  std::this_thread::sleep_for(std::chrono::milliseconds(250));
  temp.write("a.bin", 13); temp.write("c.bin", 11);
  expect(until([&]() {
    auto tree = scanner.snapshot();
    return tree->nodes[0].size == 31 && tree->files == 3 && tree->changeGen > 0;
  }), "watcher updates file size, additions and ancestors");
  fs::remove_all(temp.path / "sub");
  expect(until([&]() {
    auto tree = scanner.snapshot();
    return tree->nodes[0].size == 24 && tree->files == 2 && tree->dirs == 0 &&
           childNamed(*tree, 0, "sub") == kNone;
  }), "watcher unlinks deleted subtree and removes its complete counts");
  Scanner invalid; invalid.start(temp.file("missing"));
  expect(until([&]() { return invalid.snapshot()->complete; }), "invalid root terminates scan");
  expect(!invalid.snapshot()->error.empty() && !invalid.running(), "invalid root publishes useful error");
}

void volumeHelpers() {
#if !defined(_WIN32)
  TempDir temp;
  const std::string canonical = spaceviewer::posix::normalizePath(temp.path.u8string());
  expect(spaceviewer::posix::normalizePath(temp.path.u8string() + "/../" + temp.path.filename().u8string() + "/") ==
         canonical, "path normalization resolves dot segments and aliases");
  expect(spaceviewer::posix::normalizePath("").empty() &&
         spaceviewer::posix::normalizePath(temp.file("missing") + "///") == temp.file("missing"),
         "empty and nonexistent path normalization preserves behavior");
  std::vector<VolumeEntry> entries;
  spaceviewer::posix::addVolume(entries, canonical, "first");
  spaceviewer::posix::addVolume(entries, canonical, "second");
  expect(entries.size() == 1 && entries[0].label == "first" &&
         entries[0].total > 0, "duplicate volumes retain first label and capacity");
  expect(spaceviewer::posix::isVolumeRoot("/") && !spaceviewer::posix::isVolumeRoot(canonical),
         "root and ordinary directory classification");
#endif
}

}  // namespace

int runRefactorTests() {
  struct Case { const char* name; void (*run)(); };
  const Case cases[] = {
    {"inputEdges", inputEdges}, {"unicodeEditing", unicodeEditing}, {"cjkRanges", cjkRanges},
    {"openCommands", openCommands}, {"navigationCommands", navigationCommands}, {"detailCommands", detailCommands},
    {"scanBatches", scanBatches}, {"scanWatch", scanWatch}, {"volumeHelpers", volumeHelpers}
  };
  int failures = 0;
  for (const Case& item : cases) {
    try {
      item.run();
      std::printf("case=%s PASS\n", item.name);
    } catch (const std::exception& error) {
      ++failures;
      std::fprintf(stderr, "case=%s FAIL %s\n", item.name, error.what());
    }
  }
  std::printf("refactor cases=%zu failures=%d\n", sizeof(cases) / sizeof(cases[0]), failures);
  return failures ? 1 : 0;
}
