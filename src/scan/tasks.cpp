#include "scan/state.hpp"

#include <algorithm>
#include <iterator>

void Scanner::Impl::runTaskPool(const ScanTask& root) {
  {
    std::lock_guard<std::mutex> lock(taskMu);
    taskQueue.clear();
    taskQueue.push_back(root);
    outstandingTasks = 1;
  }
  const unsigned int hardwareThreads = std::thread::hardware_concurrency();
  const size_t threadCount = std::max<size_t>(2, std::min<size_t>(8, hardwareThreads ? hardwareThreads : 4));
  auto work = [&]() {
    lowerThreadPriority();
    for (;;) {
      ScanTask task{};
      {
        std::unique_lock<std::mutex> lock(taskMu);
        // 正常完成以 outstandingTasks 归零为准，其他线程可能仍在枚举并生成子任务。
        taskCv.wait(lock, [&]() { return cancel.load() || !taskQueue.empty() || outstandingTasks == 0; });
        if (cancel.load() || (taskQueue.empty() && outstandingTasks == 0)) return;
        if (taskQueue.empty()) continue;
        task = std::move(taskQueue.front());
        taskQueue.pop_front();
      }
      // 目录读取在队列锁外执行；scanOne 通过批量入树单独管理 treeMu。
      std::vector<ScanTask> children = scanOne(task);
      {
        std::lock_guard<std::mutex> lock(taskMu);
        if (!cancel.load()) {
          // 先登记新发现的子任务，再扣除当前任务，避免其他线程误判全部完成。
          for (ScanTask& child : children) taskQueue.push_back(std::move(child));
          outstandingTasks += children.size();
        }
        if (outstandingTasks > 0) --outstandingTasks;
        if (cancel.load()) {
          taskQueue.clear();
          outstandingTasks = 0;
        }
      }
      taskCv.notify_all();
    }
  };
  std::vector<std::thread> workers;
  workers.reserve(threadCount - 1);
  for (size_t i = 1; i < threadCount; ++i) workers.emplace_back(work);
  work();
  for (std::thread& thread : workers) thread.join();
  {
    std::lock_guard<std::mutex> lock(taskMu);
    taskQueue.clear();
    outstandingTasks = 0;
  }
}

void Scanner::Impl::scanParallel(uint32_t id, const std::string& path, bool chargePending) {
  runTaskPool(ScanTask{id, path, chargePending});
}

void Scanner::Impl::scanSerial(uint32_t id, const std::string& path, bool chargePending) {
  // 监听补扫复用枚举后端，通过显式栈遍历，避免为每个变更再创建一组扫描线程。
  std::vector<ScanTask> stack{ScanTask{id, path, chargePending}};
  while (!stack.empty() && !cancel.load()) {
    ScanTask task = std::move(stack.back());
    stack.pop_back();
    std::vector<ScanTask> children = scanOne(task);
    stack.insert(stack.end(), std::make_move_iterator(children.begin()), std::make_move_iterator(children.end()));
  }
}
