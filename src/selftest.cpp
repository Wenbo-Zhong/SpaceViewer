#include "selftest.hpp"

#include "app.hpp"
#include "export.hpp"
#include "layout.hpp"
#include "motion.hpp"
#include "scan.hpp"
#include "util.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

int gFails = 0;
std::vector<std::string> gFailureMessages;

void check(bool ok, const char* message) {
  if (!ok) {
    std::fprintf(stderr, "FAIL %s\n", message);
    ++gFails;
    gFailureMessages.emplace_back(message);
  }
}

std::string makeTemp() {
#if defined(_WIN32)
  wchar_t buffer[MAX_PATH];
  GetTempPathW(MAX_PATH, buffer);
  std::wstring dir = buffer;
  dir += L"SpaceViewerSelfTest";
  CreateDirectoryW(dir.c_str(), nullptr);
  return wideToUtf8(dir);
#else
  std::string dir = "/tmp/spaceviewer-selftest";
  mkdir(dir.c_str(), 0755);
  return dir;
#endif
}

void writeFile(const std::string& path, const std::string& bytes) {
#if defined(_WIN32)
  FILE* file = _wfopen(utf8ToWide(path).c_str(), L"wb");
#else
  FILE* file = std::fopen(path.c_str(), "wb");
#endif
  if (!file) return;
  std::fwrite(bytes.data(), 1, bytes.size(), file);
  std::fclose(file);
}

}  // namespace

int runSelfTest() {
  std::FILE* log = std::fopen("selftest-result.txt", "wb");
  if (log) {
    std::fputs("start\n", log);
    std::fclose(log);
  }
  uint64_t size = 1;
  check(parseSize("1.5K", size) && size == 1536, "parse 1.5K");
  check(parseSize("2M", size) && size == 2ull * 1024ull * 1024ull, "parse 2M");
  check(parseSize("", size) && size == 0, "parse empty");
  check(!parseSize("nope", size), "parse bad");
  check(globMatch("Photo.JPG", "*.jpg"), "glob case");
  check(!globMatch("a.txt", "*.png"), "glob miss");
  check(globMatch("abc", "a?c"), "glob question");
  std::string pretty = formatSize(1536);
  check(pretty.find("1.50") != std::string::npos || pretty.find("1.5") != std::string::npos, "format size");

  TreeSnapshot tree;
  tree.sep = '/';
  tree.rootPath = "/data";
  addNode(tree, kNone, "data", kDir, CatDir, 0);
  addNode(tree, 0, "a.txt", 0, CatDoc, 100);
  addNode(tree, 0, "b.bin", 0, CatOther, 50);
  uint32_t sub = addNode(tree, 0, "sub", kDir, CatDir, 0);
  addNode(tree, sub, "c.txt", 0, CatDoc, 25);
  accountBytes(tree, 0, 150);
  accountBytes(tree, sub, 25);
  Filter filter;
  auto visible = computeVisible(tree, filter);
  check(visible.size() == 5 && visible[1] == 100 && visible[2] == 50 && visible[3] == 25, "visible all");
  check(visible[0] == 175, "visible root");
  filter.includeGlobs.push_back("*.txt");
  visible = computeVisible(tree, filter);
  check(visible[1] == 100 && visible[2] == 0 && visible[3] == 25 && visible[0] == 125, "visible filter");
  check(nodePath(tree, sub) == "/data/sub", "path dir");
  check(nodePath(tree, 4) == "/data/sub/c.txt" || nodePath(tree, 4).find("c.txt") != std::string::npos, "path file");

  filter = Filter{};
  visible = computeVisible(tree, filter);
  auto tiles = layoutTreemap(tree, visible, 0, RectF{0, 0, 640, 480}, 16);
  check(!tiles.empty(), "layout tiles");
  double area = 0;
  bool contained = true;
  for (const Tile& tile : tiles) {
    area += tile.outer.w * tile.outer.h;
    if (tile.outer.w < 0 || tile.outer.h < 0) contained = false;
    if (tile.outer.x < -1 || tile.outer.y < -1) contained = false;
    if (tile.outer.x + tile.outer.w > 642 || tile.outer.y + tile.outer.h > 482) contained = false;
  }
  check(contained, "layout contained");
  check(area > 640.0 * 480.0 * 0.85, "layout area");

  TreeSnapshot compactTree;
  addNode(compactTree, kNone, "root", kDir, CatDir, 0);
  addNode(compactTree, 0, "large.bin", 0, CatOther, 900);
  uint32_t compactDir = addNode(compactTree, 0, "SmallApp", kDir, CatDir, 0);
  addNode(compactTree, compactDir, "Library", 0, CatOther, 60);
  addNode(compactTree, compactDir, "bin", 0, CatOther, 40);
  accountBytes(compactTree, 0, 900);
  accountBytes(compactTree, compactDir, 100);
  auto compactTiles = layoutTreemap(compactTree, computeVisible(compactTree, Filter{}), 0,
                                    RectF{0, 0, 600, 300}, 14, 8, 8);
  bool compactExpanded = false;
  for (const auto& tile : compactTiles)
    if (tile.index == compactDir && tile.container && tile.outer.w < 100) compactExpanded = true;
  check(compactExpanded, "small readable directories retain a title and show their contents");

  TreeSnapshot crowded;
  crowded.sep = '/';
  crowded.rootPath = "/c";
  addNode(crowded, kNone, "c", kDir, CatDir, 0);
  addNode(crowded, 0, "big.bin", 0, CatOther, 1000000);
  for (int n = 0; n < 200; ++n) addNode(crowded, 0, "t" + std::to_string(n), 0, CatOther, 1);
  accountBytes(crowded, 0, 1000200);
  auto crowdedVis = computeVisible(crowded, Filter{});
  auto crowdedTiles = layoutTreemap(crowded, crowdedVis, 0, RectF{0, 0, 200, 120}, 16);
  bool merged = false;
  int tiny = 0;
  for (const Tile& tile : crowdedTiles) {
    if (tile.mergeCount) merged = true;
    if (tile.size <= 1) tiny++;
  }
  check(crowdedTiles.size() == 1 && !merged && tiny == 0, "unreadable tail is omitted before layout");
  check(crowdedTiles[0].size == 1000000 && crowdedVis[0] == 1000200,
        "displayed file size and full directory total remain accurate");
  double covered = 0;
  for (const Tile& tile : crowdedTiles) covered += tile.outer.w * tile.outer.h;
  check(std::abs(covered - 24000.0) < 0.001, "readable children fill the complete parent without a tail gap");

  TreeSnapshot dense;
  addNode(dense, kNone, "dense", kDir, CatDir, 0);
  for (int i = 0; i < 150; ++i) {
    uint64_t bytes = 151 - i;
    addNode(dense, 0, "item" + std::to_string(i), 0, CatOther, bytes);
    accountBytes(dense, 0, bytes);
  }
  for (RectF box : {RectF{0, 0, 200, 120}, RectF{0, 0, 70, 300}, RectF{0, 0, 400, 38}}) {
    auto readableTiles = layoutTreemap(dense, computeVisible(dense, Filter{}), 0, box, 14, 20, 12);
    double filled = 0;
    uint64_t displayedTotal = 0;
    for (const Tile& tile : readableTiles) displayedTotal += tile.size;
    for (const Tile& tile : readableTiles) {
      check(!tile.hidden && std::floor(tile.outer.w) >= 20 && std::floor(tile.outer.h) >= 12,
            "all final rectangles have readable dimensions");
      filled += tile.outer.w * tile.outer.h;
      double expected = box.w * box.h * static_cast<double>(tile.size) / displayedTotal;
      check(std::abs(tile.outer.w * tile.outer.h - expected) < 0.001,
            "displayed siblings retain their relative proportions");
    }
    check(!readableTiles.empty() && readableTiles.size() < 150 && std::abs(filled - box.w * box.h) < 0.001,
          "dense wide and narrow layouts have neither unreadable rectangles nor empty area");
  }

  TreeSnapshot deep;
  addNode(deep, kNone, "root", kDir, CatDir, 0);
  uint32_t parent = 0;
  for (int level = 0; level < 8; ++level) {
    uint32_t next = addNode(deep, parent, "directory", kDir, CatDir, 0);
    addNode(deep, parent, "side.bin", 0, CatOther, 100);
    accountBytes(deep, parent, 100);
    parent = next;
  }
  addNode(deep, parent, "large.bin", 0, CatOther, 100000);
  accountBytes(deep, parent, 100000);
  auto deepTiles = layoutTreemap(deep, computeVisible(deep, Filter{}), 0,
                                 RectF{0, 0, 1600, 1000}, 18, 8, 8);
  for (const Tile& tile : deepTiles) {
    if (tile.index == kNone) continue;
    int depth = 0;
    for (uint32_t n = tile.index; deep.nodes[n].parent != kNone; n = deep.nodes[n].parent) ++depth;
    check(depth <= 3, "overview depth limited to three levels");
  }
  TreeSnapshot chain;
  addNode(chain, kNone, "root", kDir, CatDir, 0);
  uint32_t chainDir = addNode(chain, 0, "Steam", kDir, CatDir, 0);
  uint32_t chainSub = addNode(chain, chainDir, "steamapps", kDir, CatDir, 0);
  addNode(chain, chainSub, "data.pak", 0, CatOther, 100);
  accountBytes(chain, chainSub, 100);
  auto chainTiles = layoutTreemap(chain, computeVisible(chain, Filter{}), 0,
                                  RectF{0, 0, 640, 480}, 18, 8, 8);
  check(chainTiles.size() == 1 && chainTiles[0].index == chainDir,
        "single-child directory remains a readable block");

  auto expandedChain = layoutTreemap(chain, computeVisible(chain, Filter{}), 0,
                                     RectF{0, 0, 640, 480}, 18, 8, 8, {chainDir});
  check(expandedChain.size() == 2 && expandedChain[0].index == chainDir &&
        expandedChain[1].index == chainSub, "single-child folder expands locally");
  check(expandedChain[0].outer.w == chainTiles[0].outer.w &&
        expandedChain[0].outer.h == chainTiles[0].outer.h,
        "local expansion preserves folder area");
  auto expandedDeep = layoutTreemap(chain, computeVisible(chain, Filter{}), 0,
                                    RectF{0, 0, 640, 480}, 18, 8, 8, {chainDir, chainSub});
  check(expandedDeep.size() == 3 && expandedDeep.back().index == 3,
        "explicit expansion reveals the next path component");

  uint32_t branch = addNode(chain, 0, "Games", kDir, CatDir, 0);
  uint32_t branchChild = addNode(chain, branch, "Game", kDir, CatDir, 0);
  auto activePath = expansionPath(chain, 0, chainSub);
  check(activePath.size() == 2 && activePath[0] == chainDir && activePath[1] == chainSub,
        "expanding a descendant retains its ancestors");
  activePath = expansionPath(chain, 0, branchChild);
  check(activePath.size() == 2 && activePath[0] == branch && activePath[1] == branchChild &&
        std::find(activePath.begin(), activePath.end(), chainDir) == activePath.end(),
        "switching branches retracts previous manual expansion");
  activePath = expansionPath(chain, 0, branch);
  check(activePath.size() == 1, "selecting an ancestor retracts deeper detail");

  Tile localParent;
  localParent.index = 10; localParent.parent = 0; localParent.flags = kDir;
  localParent.outer = {10, 20, 300, 400};
  Tile localNeighbor = localParent;
  localNeighbor.index = 11; localNeighbor.outer.x = 310;
  Tile localChild;
  localChild.index = 12; localChild.parent = 10;
  localChild.outer = {13, 38, 294, 379};
  TileMotion localMotion;
  localMotion.setTarget({localParent, localNeighbor}, 0, 0, {10, 20, 600, 400});
  localParent.title = {10, 20, 300, 18}; localParent.container = true;
  localMotion.setTarget({localParent, localChild, localNeighbor}, 1, 0, {10, 20, 600, 400}, true);
  for (const Tile& tile : localMotion.sample(1)) {
    if (tile.index == 12) {
      check(tile.outer.x == localChild.outer.x && tile.outer.w == localChild.outer.w &&
            tile.outer.y == localChild.outer.y && tile.outer.h == localChild.outer.h && tile.labelOpacity == 1,
            "local detail appears at full size and opacity on the click frame");
    }
    if (tile.index == 11) check(tile.outer.x == 310 && tile.outer.w == 300,
                               "local reveal leaves neighbors fixed");
  }
  check(!localMotion.running(1), "single click schedules no animation frames");
  localMotion.setTarget({localNeighbor}, 1.01, 0, {10, 20, 600, 400}, true);
  check(localMotion.sample(1.01).size() == 1 && !localMotion.running(1.01),
        "branch retraction is immediate without outgoing tiles");

  // Navigation starts in the clicked region, keeps surrounding tiles until
  // they leave the viewport, and can be reversed before the animation ends.
  RectF viewport{10, 20, 600, 400};
  Tile folder;
  folder.index = 1; folder.parent = 0; folder.flags = kDir;
  folder.outer = {10, 20, 300, 400};
  Tile sibling = folder;
  sibling.index = 2; sibling.outer = {310, 20, 300, 400};
  Tile child;
  child.index = 3; child.parent = 1;
  child.outer = viewport;
  TileMotion motion;
  motion.setTarget({folder, sibling}, 0, 0, viewport);
  motion.setTarget({child}, 1, 1, viewport);
  auto startTiles = motion.sample(1);
  check(startTiles.size() == 3, "zoom retains outgoing context");
  for (const Tile& tile : startTiles) {
    if (tile.index == 3) {
      check(std::abs(tile.outer.x - folder.outer.x) < 0.001 &&
            std::abs(tile.outer.w - folder.outer.w) < 0.001,
            "new child starts inside clicked folder");
      check(tile.labelOpacity == 0, "new labels fade in");
    }
  }
  for (double time : {1.0, 1.05, 1.2, 1.41}) {
    for (const Tile& tile : motion.sample(time)) {
      check(tile.outer.x >= viewport.x && tile.outer.y >= viewport.y &&
            tile.outer.x + tile.outer.w <= viewport.x + viewport.w + 0.001 &&
            tile.outer.y + tile.outer.h <= viewport.y + viewport.h + 0.001,
            "zoom stays inside map");
    }
  }
  auto finishTiles = motion.sample(1.5);
  check(finishTiles.size() == 1 && finishTiles[0].outer.w == viewport.w &&
        !motion.running(1.5), "zoom finishes at exact target");
  motion.setTarget({folder, sibling}, 2, 0, viewport);
  check(motion.sample(2).back().exiting, "return retains shrinking content above parent");
  auto beforeInterrupt = motion.sample(2.1);
  motion.setTarget({child}, 2.1, 1, viewport);
  auto afterInterrupt = motion.sample(2.1);
  for (const Tile& tile : afterInterrupt) {
    if (tile.index == 1 && tile.exiting) {
      for (const Tile& old : beforeInterrupt) {
        if (old.index == 1 && !old.exiting)
          check(std::abs(tile.outer.w - old.outer.w) < 0.001,
                "interrupted zoom continues from current geometry");
      }
    }
  }

  std::string root = makeTemp();
  std::string sep = 
#if defined(_WIN32)
      "\\";
#else
      "/";
#endif
  writeFile(root + sep + "a.txt", std::string(100, 'a'));
  writeFile(root + sep + "b.bin", std::string(50, 'b'));
#if defined(_WIN32)
  CreateDirectoryW(utf8ToWide(root + sep + "sub").c_str(), nullptr);
#else
  mkdir((root + "/sub").c_str(), 0755);
#endif
  writeFile(root + sep + "sub" + sep + "c.txt", std::string(25, 'c'));
  writeFile(root + sep + "\u6d4b\u8bd5.txt", std::string(10, 'z'));

  Scanner scanner;
  scanner.start(root);
  for (int i = 0; i < 200 && scanner.running(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  auto snap = scanner.snapshot();
  check(snap && snap->complete, "scan complete");
  check(snap && snap->files >= 4, "scan files");
  check(snap && snap->error.empty(), "scan error");
  uint64_t total = 0;
  if (snap) {
    for (const Node& node : snap->nodes) {
      if ((node.flags & kDir) == 0) total += node.size;
    }
  }
  check(total == 185, "scan bytes");

  // Verify the completed scan continues to track real on-disk size changes.
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  uint64_t beforeGeneration = snap ? snap->changeGen : 0;
  writeFile(root + sep + "sub" + sep + "c.txt", std::string(225, 'c'));
  bool watchedGrowth = false;
  for (int i = 0; i < 150; ++i) {
    auto updated = scanner.snapshot();
    if (updated && updated->changeGen > beforeGeneration && updated->nodes[0].size == 385 &&
        !updated->changed.empty()) { watchedGrowth = true; break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  check(watchedGrowth, "post-scan watcher updates ancestor size and reports changed path");
  writeFile(root + sep + "sub" + sep + "c.txt", std::string(25, 'c'));
  bool watchedShrink = false;
  for (int i = 0; i < 150; ++i) {
    auto updated = scanner.snapshot();
    if (updated && updated->nodes[0].size == 185) { watchedShrink = true; break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  check(watchedShrink, "post-scan watcher tracks shrinking files");

  uint32_t refreshIndex = kNone;
  auto beforeRefresh = scanner.snapshot();
  if (beforeRefresh) for (uint32_t i = 0; i < beforeRefresh->nodes.size(); ++i)
    if (nodeName(*beforeRefresh,i) == "sub") refreshIndex = i;
  scanner.refreshSubtree(refreshIndex);
  for (int i = 0; i < 150 && scanner.running(); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  auto refreshed = scanner.snapshot();
  check(refreshed && refreshed->complete && refreshed->rootPath == root &&
        refreshed->nodes[0].size == 185 && refreshed->nodes[refreshIndex].size == 25,
        "selected-subtree refresh retains scan root and updates ancestor totals");

  std::string snapshotFile = root + sep + "scan.svs";
  std::string snapshotError;
  check(refreshed && writeSnapshot(*refreshed, snapshotFile, snapshotError), "save scan snapshot");
  TreeSnapshot restored;
  check(readSnapshot(snapshotFile, restored, snapshotError) && restored.rootPath == snap->rootPath &&
        restored.nodes.size() == snap->nodes.size() && restored.nodes[0].size == snap->nodes[0].size,
        "snapshot round trip preserves the complete directory tree and sizes");
  writeFile(root + sep + "bad.svs", "SpaceViewer-Snapshot-1\ninvalid");
  TreeSnapshot invalidSnapshot;
  check(!readSnapshot(root + sep + "bad.svs", invalidSnapshot, snapshotError), "invalid snapshot rejected");
#if defined(_WIN32)
  DeleteFileW(utf8ToWide(snapshotFile).c_str());
  DeleteFileW(utf8ToWide(root + sep + "bad.svs").c_str());
#else
  std::remove(snapshotFile.c_str()); std::remove((root + sep + "bad.svs").c_str());
#endif

  App clickApp;
  Host clickHost;
  std::string clickTitle;
  clickHost.setTitle = [&](const std::string& title) { clickTitle = title; };
  Input clickInput;
  clickInput.windowW = 1200; clickInput.windowH = 850;
  clickInput.mouseX = -1; clickInput.mouseY = -1;
  Frame clickFrame;
  clickApp.scanPath(root);
  bool foundFolder = false;
  for (int i = 0; i < 100 && !foundFolder; ++i) {
    clickInput.timeSec = i * 0.03;
    clickApp.update(clickInput, clickHost, clickFrame);
    for (const auto& text : clickFrame.texts) if (text.utf8 == "sub") foundFolder = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  clickInput.timeSec = 4;
  clickApp.update(clickInput, clickHost, clickFrame);
  for (const auto& text : clickFrame.texts) {
    if (text.utf8 == "sub") {
      clickInput.mouseX = text.x + text.w / 2;
      clickInput.mouseY = text.y + text.h / 2;
    }
  }
  check(foundFolder && clickInput.mouseX >= 0, "folder available for click regression");
  std::string overviewTitle = clickTitle;
  clickInput.leftPressed = true;
  clickInput.timeSec = 4.1;
  clickApp.update(clickInput, clickHost, clickFrame);
  check(clickTitle == overviewTitle, "single click preserves current root");
  clickInput.timeSec = 4.2;
  clickApp.update(clickInput, clickHost, clickFrame);
  check(clickTitle != overviewTitle && clickTitle.find("sub") != std::string::npos,
        "double click enters original folder even after local expansion");

  clickInput.leftPressed = false;
  clickInput.timeSec = 4.5;
  clickApp.update(clickInput, clickHost, clickFrame);
  bool rootHeader = false;
  bool fileBelowHeader = false;
  int rootHeaderBottom = 0;
  for (const auto& text : clickFrame.texts) {
    if (text.utf8.find("sub  ") == 0) {
      rootHeader = true;
      rootHeaderBottom = text.y + text.h;
    }
  }
  for (const auto& text : clickFrame.texts) {
    if (text.utf8 == "c.txt" && text.y >= rootHeaderBottom) fileBelowHeader = true;
  }
  check(rootHeader && fileBelowHeader, "file-only directory retains root header above its files");
  clickInput.windowW = 700; clickInput.windowH = 500; clickInput.dpi = 144;
  clickInput.timeSec = 5;
  clickApp.update(clickInput, clickHost, clickFrame);
  rootHeader = false;
  for (const auto& text : clickFrame.texts)
    if (text.utf8.find("sub  ") == 0) rootHeader = true;
  check(rootHeader, "root header survives resize and DPI change");

  clickInput.mouseX = -1; clickInput.mouseY = -1; clickInput.timeSec = 6;
  clickApp.update(clickInput, clickHost, clickFrame);
  int sampleX = -1, sampleY = -1;
  for (const auto& text : clickFrame.texts) {
    if (text.utf8 == "c.txt") { sampleX = text.x + text.w / 2; sampleY = text.y; }
  }
  if (sampleX >= 0 && sampleY >= 0) {
    auto pixelAt = [&]() { return (clickFrame.bgra[static_cast<size_t>(sampleY) * clickFrame.width + sampleX] >> 16) & 255; };
    uint32_t baseBrightness = pixelAt();
    clickInput.mouseX = sampleX; clickInput.mouseY = sampleY; clickInput.timeSec = 6.01;
    clickApp.update(clickInput, clickHost, clickFrame);
    uint32_t peakBrightness = pixelAt();
    clickInput.timeSec = 6.21;
    clickApp.update(clickInput, clickHost, clickFrame);
    uint32_t settledBrightness = pixelAt();
    check(peakBrightness > settledBrightness && settledBrightness > baseBrightness,
          "hover peaks then settles above ordinary brightness");
    check(!clickApp.animating(), "settled hover does not continuously repaint");
    check(settledBrightness >= baseBrightness + 20, "held hover remains clearly brighter than ordinary tiles");
    clickInput.timeSec = 6.8;
    clickApp.update(clickInput, clickHost, clickFrame);
    check(pixelAt() == settledBrightness, "held hover stays highlighted after the initial pulse");
    clickInput.mouseX = -1; clickInput.mouseY = -1; clickInput.timeSec = 6.9;
    clickApp.update(clickInput, clickHost, clickFrame);
    check(pixelAt() == baseBrightness, "leaving hover restores original brightness");
  } else check(false, "hover brightness fixture visible");

  ViewOptions viewOptions = clickApp.options();
  int oldDetail = viewOptions.detail;
  clickInput.command = static_cast<int>(HotKind::MoreDetail); clickInput.timeSec = 7;
  clickApp.update(clickInput, clickHost, clickFrame);
  check(clickApp.options().detail == oldDetail + 1, "toolbar and menu command increase detail");
  clickInput.command = static_cast<int>(HotKind::FileGroups);
  clickApp.update(clickInput, clickHost, clickFrame);
  check(clickApp.options().fileGroups, "file group command enables type colors");
  clickInput.command = static_cast<int>(HotKind::ShowFree);
  clickApp.update(clickInput, clickHost, clickFrame);
  check(clickApp.options().showFree, "free-space command toggles display option");
  clickInput.command = static_cast<int>(HotKind::ShowUnknown);
  clickApp.update(clickInput, clickHost, clickFrame);
  check(clickApp.options().showUnknown, "unknown-space command toggles display option");
  clickInput.command = static_cast<int>(HotKind::Home);
  clickApp.update(clickInput, clickHost, clickFrame);
  check(clickTitle == overviewTitle, "home command returns to original scan root");
  clickInput.command = -1;

  std::FILE* end = std::fopen("selftest-result.txt", "wb");
  if (end) {
    if (gFails == 0) std::fputs("ok\n", end);
    else {
      std::fprintf(end, "failed %d\n", gFails);
      for (const auto& message : gFailureMessages) std::fprintf(end, "%s\n", message.c_str());
    }
    std::fclose(end);
  }
  if (gFails == 0) {
    std::printf("self-test ok\n");
    return 0;
  }
  std::printf("%d checks failed\n", gFails);
  return 1;
}
