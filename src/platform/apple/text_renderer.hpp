#pragma once

#include "view/frame.hpp"

#include <memory>

namespace spaceviewer::apple {

// AppKit drawing stays on the view's main thread.
class TextRenderer {
 public:
  TextRenderer();
  ~TextRenderer();
  void draw(const std::vector<Frame::Text>& texts, double scale, double canvasHeight);
  size_t cachedEntries() const;
  size_t cachedTextCost() const;

  static constexpr size_t entryLimit = 2048;
  static constexpr size_t textCostLimit = 2 * 1024 * 1024;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace spaceviewer::apple
