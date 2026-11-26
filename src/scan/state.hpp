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

// Private scanner state shared by the coordinator and native backends.
// Backend tree mutations hold treeMu; publication retains its original lock order.
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

  // Exactly one enumeration backend and one watcher backend are linked per host.
  static void lowerThreadPriority();
  bool initializeRoot(const std::string& root);
  std::vector<ScanTask> scanOne(const ScanTask& task);
  void watch();
#if defined(__APPLE__)
  void reconcileApple(const std::string& root, char** paths, size_t count);
#endif
};
