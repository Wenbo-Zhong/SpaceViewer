#pragma once

#include "scan.hpp"
#include "types.hpp"

#include <string>

class App {
 public:
  App();
  ~App();

  void setWake(std::function<void()> wake);
  void scanPath(const std::string& path);
  void update(const Input& input, const Host& host, Frame& frame);
  bool animating() const;
  bool ibeam(int x, int y) const;
  bool takeMenu(MenuRequest& out);
  void applyMenu(MenuCommand command, const Host& host);

 private:
  struct State;
  State* state;
};
