#include "selftest.hpp"

#include "layout.hpp"
#include "scan.hpp"
#include "util.hpp"

#include <chrono>
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

void check(bool ok, const char* message) {
  if (!ok) {
    std::fprintf(stderr, "FAIL %s\n", message);
    ++gFails;
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
  check(!crowdedTiles.empty() && !merged && tiny == 0, "tiny files stay in the parent");

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

  std::FILE* end = std::fopen("selftest-result.txt", "wb");
  if (end) {
    if (gFails == 0) std::fputs("ok\n", end);
    else std::fprintf(end, "failed %d\n", gFails);
    std::fclose(end);
  }
  if (gFails == 0) {
    std::printf("self-test ok\n");
    return 0;
  }
  std::printf("%d checks failed\n", gFails);
  return 1;
}
