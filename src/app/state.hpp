#pragma once

#include "app/app.hpp"
#include "app/edit.hpp"
#include "model/filter.hpp"
#include "scan/scanner.hpp"
#include "view/motion.hpp"
#include "view/layout_cache.hpp"

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>

struct App::State {
  Scanner scanner;
  std::function<void()> wake;
  std::mutex noteMu;
  std::thread exportThread;
  std::atomic<bool> exportRunning{false};
  std::string note;
  double noteUntil = 0;
  Edit path, inc, exc, min;
  bool minInvalid = false;
  Focus focus = Focus::None;
  Filter filter;
  std::vector<HotRect> hots;
  std::vector<Tile> tiles;
  TileMotion motion;
  TreemapLayoutCache layoutCache;
  bool frameAnim = false;
  ViewOptions options;
  bool archived = false;
  bool permissionWarningShown = false;
  uint64_t seenChange = 0;
  std::unordered_map<uint32_t, double> changes;
  uint64_t hovered = kNone;
  double hoverSince = 0;
  std::vector<uint32_t> back, forward;
  std::vector<VolumeEntry> drives;
  bool driveOpen = false;
  uint32_t view = 0;
  std::vector<uint32_t> expanded;
  bool snapLayout = false;
  uint32_t selected = kNone;
  uint32_t menuNode = kNone;
  bool menuPending = false;
  MenuRequest menu;
  std::string lastTitle;
  double lastClick = -1;
  int lastX = 0, lastY = 0;
  bool lastZoom = false;
  std::shared_ptr<const TreeSnapshot> shown;
  std::vector<uint64_t> visible;
  uint64_t visibleRevision = 0;
  const TreeSnapshot* visibleKey = nullptr;
  std::string visibleInc, visibleExc;
  uint64_t visibleMin = 0;

  ~State();
  void setNote(const std::string& text, double now);
  std::string currentNote(double now);
  void scan(const std::string& pathText);
  Edit* focusedEdit();
  void refreshFilter();
  void navigate(uint32_t index);
  std::string pathOf(uint32_t index) const;
  void copyIndex(uint32_t index, const Host& host, double now);
  void refreshSnapshot(const Host& host);
  void updateTitle(const Host& host);
  void handleInput(const Input& input, const Host& host);
  void execute(HotKind kind, const Host& host, double now);
  void clickTile(const HotRect& hot, const Input& input, const Host& host,
                 double previousClick, int previousX, int previousY, uint32_t previousSelected);
  void render(const Input& input, const Host& host, Frame& frame);
  int renderControls(const Input& input, const Host& host, Frame& frame, std::vector<HotRect>& next);
  void renderTreemap(const Input& input, Frame& frame, std::vector<HotRect>& next);
  void renderStatus(const Input& input, Frame& frame, const std::vector<HotRect>& next);
  void renderDrivePopup(const Input& input, Frame& frame, std::vector<HotRect>& next, int driveX);
};
