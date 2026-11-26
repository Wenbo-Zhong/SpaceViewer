#pragma once

#include "view/frame.hpp"

#include <memory>

namespace spaceviewer::apple {

// AppKit 绘制与缓存访问均由视图主线程执行，缓存不提供跨线程同步。
class TextRenderer {
 public:
  TextRenderer();
  ~TextRenderer();
  void draw(const std::vector<Frame::Text>& texts, double scale, double canvasHeight);
  size_t cachedEntries() const;
  size_t cachedTextCost() const;

  // 同时限制标签数量与估算文本成本；成本不等于原生对象的精确内存占用。
  static constexpr size_t entryLimit = 2048;
  static constexpr size_t textCostLimit = 2 * 1024 * 1024;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace spaceviewer::apple
