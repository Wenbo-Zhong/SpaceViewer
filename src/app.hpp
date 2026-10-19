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
  void requestStop();
  void update(const Input& input, const Host& host, Frame& frame);
  bool animating() const;
  ViewOptions options() const;
  void setOptions(const ViewOptions& options);
  bool saveSnapshot(const std::string& path, std::string& error) const;
  bool loadSnapshot(const std::string& path, std::string& error);
  bool ibeam(int x, int y) const;
  bool takeMenu(MenuRequest& out);
  void applyMenu(MenuCommand command, const Host& host);

 private:
  struct State;
  State* state;
};
