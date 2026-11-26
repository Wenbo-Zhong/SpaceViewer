#pragma once

#include "scan/scanner.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#if defined(__linux__)
#include <sys/types.h>
#endif

namespace spaceviewer::scan {
constexpr int kBatch = 2000;
std::string normalizePath(const std::string& input);
bool isVolumeRoot(const std::string& path);
std::string joinPath(const std::string& dir, const std::string& name, char sep);
void bubble(TreeSnapshot& tree, uint32_t start, int64_t delta);
void unlinkNode(TreeSnapshot& tree, uint32_t id);
void countSubtree(const TreeSnapshot& tree, uint32_t id, uint64_t& files, uint64_t& dirs);
}  // namespace spaceviewer::scan

// 协调线程与原生后端共享的内部状态，UI 仅通过 Scanner 的公开接口访问。
// treeMu 保护 live，pubMu 保护 published，publishMu 串行化发布时机。
// 发布时持有 publishMu，先获取并释放 treeMu，再获取 pubMu；持有 treeMu 时不得调用 publish。
// wakeMu 仅保护通知回调，taskMu 保护任务队列和 outstandingTasks。
struct Scanner::Impl {
  std::mutex treeMu;
  std::mutex pubMu;
  std::mutex publishMu;
  std::mutex wakeMu;
  std::mutex taskMu;
  std::condition_variable taskCv;
  struct ScanTask {
    uint32_t node;
    std::string path;
    // 初次扫描从未知空间扣减；子树刷新和监听补扫只累计实际大小。
    bool chargePending;
  };
  struct Entry {
    std::string name;
    uint64_t size = 0;
    uint8_t category = CatOther;
    bool dir = false;
    bool traverse = true;
    std::string path;
  };
  std::deque<ScanTask> taskQueue;
  // 同时包含排队及正在执行的目录任务；队列暂空并不代表整次扫描结束。
  size_t outstandingTasks = 0;
  TreeSnapshot live;
  std::shared_ptr<const TreeSnapshot> published = std::make_shared<TreeSnapshot>();
  std::function<void()> wake;
  std::thread worker;
  std::atomic<bool> cancel{false};
  std::atomic<bool> runningFlag{false};
  std::chrono::steady_clock::time_point started{};
  std::chrono::steady_clock::time_point lastPub{};
#if defined(__linux__)
  bool stayOnRootDevice = false;
  dev_t rootDevice = 0;
#endif

  ~Impl();
  void join();
  void fire();
  void publish(bool force);
  void threadMain();
  void runTaskPool(const ScanTask& root);
  void scanParallel(uint32_t id, const std::string& path, bool chargePending);
  void scanSerial(uint32_t id, const std::string& path, bool chargePending);
  void flushBatch(uint32_t id, bool chargePending, std::vector<Entry>& batch,
                  std::vector<ScanTask>& children);
  uint32_t removeNode(uint32_t id);
  void initializeVolume(uint64_t capacity, uint64_t available);

  // 每个平台仅链接一套目录枚举后端和一套监听后端，共用上层任务池与树更新逻辑。
  static void lowerThreadPriority();
  bool initializeRoot(const std::string& root);
  std::vector<ScanTask> scanOne(const ScanTask& task);
  void watch();
#if defined(__APPLE__)
  void reconcileApple(const std::string& root, char** paths, size_t count);
#endif
};
