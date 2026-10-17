#pragma once

#include "types.hpp"

#include <functional>
#include <memory>
#include <string>

class Scanner {
 public:
  Scanner();
  ~Scanner();
  Scanner(const Scanner&) = delete;
  Scanner& operator=(const Scanner&) = delete;

  void setWake(std::function<void()> wake);
  void start(const std::string& utf8Path);
  void requestStop();
  bool running() const;
  bool stopRequested() const;
  std::shared_ptr<const TreeSnapshot> snapshot() const;

 private:
  struct Impl;
  Impl* impl;
};
