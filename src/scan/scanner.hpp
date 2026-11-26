#pragma once

#include "model/tree.hpp"

#include <functional>
#include <memory>
#include <string>

// 后台扫描与系统监听的统一入口，生命周期操作由宿主串行发起。
// 停止或重新启动会通过协作式取消退出后台任务，UI 读取已发布快照而非工作树。
class Scanner {
 public:
  Scanner();
  ~Scanner();
  Scanner(const Scanner&) = delete;
  Scanner& operator=(const Scanner&) = delete;

  // 回调可由多个扫描线程触发，应只通知宿主刷新，不同步重入扫描生命周期。
  void setWake(std::function<void()> wake);
  void start(const std::string& utf8Path);
  // 只发出取消请求，不等待退出；start、loadSnapshot 和析构会等待旧任务结束。
  void requestStop();
  void refreshSubtree(uint32_t index);
  void loadSnapshot(TreeSnapshot tree);
  // 表示目录枚举是否进行中；枚举结束后后台线程仍可能负责文件监听。
  bool running() const;
  bool stopRequested() const;
  // 获取最近发布的不可变快照，调用者可持有它跨越后续扫描或刷新。
  std::shared_ptr<const TreeSnapshot> snapshot() const;

 private:
  struct Impl;
  Impl* impl;
};
