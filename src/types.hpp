#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

constexpr uint32_t kNone = 0xffffffffu;

enum NodeFlags : uint8_t {
  kDir = 1,
  kFree = 2,
  kPending = 4
};

enum Category : uint8_t {
  CatOther = 0,
  CatDir,
  CatImage,
  CatVideo,
  CatAudio,
  CatArchive,
  CatDoc,
  CatCode,
  CatExe,
  CatFree
};

struct Node {
  uint32_t nameOff = 0;
  uint32_t nameLen = 0;
  uint32_t parent = kNone;
  uint32_t firstChild = kNone;
  uint32_t lastChild = kNone;
  uint32_t nextSibling = kNone;
  uint32_t childCount = 0;
  uint64_t size = 0;
  uint8_t flags = 0;
  uint8_t category = CatOther;
};

struct TreeSnapshot {
  std::vector<Node> nodes;
  std::string nameBlob;
  std::string rootPath;
  std::string error;
  std::string tip;
  char sep = '/';
  uint32_t pending = kNone;
  uint64_t capacity = 0;
  uint64_t freeBytes = 0;
  uint64_t files = 0;
  uint64_t dirs = 0;
  uint64_t errors = 0;
  double elapsed = 0;
  bool running = false;
  bool complete = false;
  bool aborted = false;
  bool watchGap = false;
  uint64_t changeGen = 0;
  std::vector<uint32_t> changed;
};

struct VolumeEntry {
  std::string path;
  std::string label;
  uint64_t total = 0;
  uint64_t free = 0;
};

struct Filter {
  std::vector<std::string> includeGlobs;
  std::vector<std::string> excludeGlobs;
  uint64_t minSize = 0;
};

struct RectF {
  double x = 0, y = 0, w = 0, h = 0;
};

struct Tile {
  uint32_t index = kNone;
  uint32_t parent = kNone;
  uint64_t size = 0;
  uint32_t mergeCount = 0;
  uint8_t flags = 0;
  uint8_t category = CatOther;
  RectF outer;
  RectF title;
  double labelOpacity = 1;
  bool exiting = false;
  bool container = false;
};

enum class HotKind {
  Open,
  Drives,
  Back,
  Forward,
  Up,
  Rescan,
  Stop,
  Export,
  EditPath,
  EditInc,
  EditExc,
  EditMin,
  DriveItem,
  Tile
};

struct HotRect {
  int x = 0, y = 0, w = 0, h = 0;
  HotKind kind = HotKind::Tile;
  uint32_t index = kNone;
  int driveIndex = -1;
};

struct Input {
  int windowW = 0;
  int windowH = 0;
  float dpi = 96.f;
  int mouseX = 0;
  int mouseY = 0;
  bool leftPressed = false;
  bool rightPressed = false;
  bool keyBackspace = false;
  bool keyDelete = false;
  bool keyEnter = false;
  bool keyTab = false;
  bool keyEsc = false;
  bool keyLeft = false;
  bool keyRight = false;
  bool keyHome = false;
  bool keyEnd = false;
  bool keyF5 = false;
  bool keyUpNav = false;
  bool keyBackNav = false;
  bool keyForwardNav = false;
  bool modCtrl = false;
  bool modShift = false;
  bool modAlt = false;
  bool copy = false;
  bool paste = false;
  bool selectAll = false;
  bool openShortcut = false;
  std::string typed;
  double timeSec = 0;
};

struct MenuRequest {
  int x = 0;
  int y = 0;
  bool canOpen = false;
  bool canReveal = false;
  bool canCopy = false;
  bool canZoom = false;
};

enum class MenuCommand { None = 0, Open = 1, Reveal = 2, CopyPath = 3, Zoom = 4 };

struct Host {
  std::function<std::vector<VolumeEntry>()> volumes;
  std::function<bool(std::string& path)> pickFolder;
  std::function<bool(std::string& path)> pickSave;
  std::function<void(const std::string& path)> openPath;
  std::function<void(const std::string& path)> revealPath;
  std::function<void(const std::string& text)> setClipboard;
  std::function<std::string()> getClipboard;
  std::function<void(const std::string& title)> setTitle;
};

struct Frame {
  int width = 0;
  int height = 0;
  std::vector<uint32_t> bgra;
  struct Text {
    int x = 0, y = 0, w = 0, h = 0;
    int fontPx = 14;
    uint32_t rgb = 0;
    bool center = false;
    std::string utf8;
  };
  std::vector<Text> texts;

  void resize(int w, int h, uint32_t clear);
};

inline constexpr uint32_t packRgb(uint32_t rgb) {
  return 0xff000000u | (rgb & 0x00ffffffu);
}
