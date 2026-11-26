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

// UI 状态主要由宿主线程维护；导出线程仅共享结果提示及完成状态。
// 后台导出线程更新结果提示时持有 noteMu，exportRunning 使用原子状态跨线程通知。
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
  // 保留正在展示的不可变快照，使节点编号、可见大小与这一帧的命中区域相互对应。
  std::shared_ptr<const TreeSnapshot> shown;
  std::vector<uint64_t> visible;
  // 每次重新计算过滤结果都递增，供布局缓存判断可见大小是否已变化。
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
